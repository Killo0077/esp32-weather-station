/*
  ESP32 + ILI9341 + Joystick + DHT22
  Horizontal UI (320x240)
  Pages:
   - Menu
   - Local Weather (DHT22)
   - Cork Airport (Met.ie CSV download)
   - BBC News (RSS titles)
   - UK News (Guardian UK RSS titles)
   - Clock (Ireland local time via NTP)

  Fixes included:
   - WiFiMulti: multiple hotspots
   - HTTPClient follow redirects + force Accept-Encoding: identity (avoid gzip issues)
   - RSS CDATA stripping
*/

// ===================== IMPORTANT (Arduino prototype fix) =====================
// Put enum BEFORE includes so Arduino's auto-generated prototypes know "Page".
enum Page
{
    PAGE_MENU,
    PAGE_HOME,
    PAGE_LIGHTS,
    PAGE_LOCAL,
    PAGE_CORK,
    PAGE_WARN,
    PAGE_BBC,
    PAGE_UK
};

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <time.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Adafruit_BME280.h>
#include <Wire.h>

//===========BMP sensor temp===================
Adafruit_BME280 bme;

// ===================== WIFI HOSTPOT =====================
WiFiMulti wifiMulti;

static const char *WIFI1_SSID = "xxxxxx";
#Device name static const char *WIFI1_PASS = "xxxxxxxx";
#Device password

    static const char *WIFI2_SSID = "";
static const char *WIFI2_PASS = "";

// ===================== URLS =====================
static const char *URL_CORK_CSV = "https://www.met.ie/latest-reports/observations/download/cork";
static const char *URL_METWARN_RSS = "https://www.met.ie/warningsxml/rss.xml";
static const char *URL_BBC_RSS = "https://feeds.bbci.co.uk/news/rss.xml";
static const char *URL_UK_RSS = "https://feeds.bbci.co.uk/news/uk/rss.xml";

//============ TOUCH + BUZZER =========== (New charming face when touch the sensor, sound and light effect)
#define TOUCH_PIN 27
#define BUZZER_PIN 14
#define TOUCH_ACTIVE_HIGH true

// ===================== TFT =====================
// NOTE: keep your wiring/pins as-is (you said it works)
#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

;

// ===================== BUTTONS =====================
#define BTN_UP 25
#define BTN_DOWN 26
#define BTN_SEL 33

unsigned long lastUpMs = 0;
unsigned long lastDownMs = 0;
unsigned long lastSelMs = 0;

bool btnPressed(int pin, unsigned long &lastMs, unsigned long debounceMs = 180)
{
    if (digitalRead(pin) == LOW)
    {
        unsigned long now = millis();
        if (now - lastMs > debounceMs)
        {
            lastMs = now;
            return true;
        }
    }
    return false;
}

// ============== LED STRIP =================
#define LED_PIN 13
#define NUM_LED 18
#define LED_TYPE WS2812
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

enum LedEffect
{
    FX_CYBER = 0;
    FX_CALM,
    FX_RAINBOW,
    FX_SEXY_RED,
    FX_LAMP_WARM,
    FX_LAMP_WHITE,
    FX_OFF
};

const char *effectNames[] = {
    "CYBER",
    "CALM PINK"
    "RAINBOW"
    "SEXY RED"
    "LAMP WARM"
    "LAMP WHITE"
    "OFF"};

const int FX_COUNT = sizeof(effectNames) / sizeof(effectNames[0]);
int currentEffect = FX_CYBER;

unsigned long lastLedMs = 0;
uint8_t fxHue = 0;
uint8_t fxBreath = 10;
int fxDir = 1;

// ======= LOVE MODE=============== (single press touch sensor light and sound effect for a few seconds)
bool loveMode = false;
unsigned long loveModeUntil = 0;

// touch debounce / boot ignore
bool touchRawLast = false;
bool touchStable = false;
unsigned long touchLastChangeMs = 0;
unsigned long ignoreTouchUntil = 0;

// ===================== UI STATE =====================
Page page = PAGE_HOME;
Page lastDrawnPage = (Page)255;

struct Item
{
    const char *label;
    Page target;
};
Item items[] = {
    {"HOME CLOCK", PAGE_HOME},
    {"LIGHTS", PAGE_LIGHTS},
    {"LOCAL SENSOR", PAGE_LOCAL},
    {"CORK AIRPORT", PAGE_CORK},
    {"MET WARNINGS", PAGE_WARN},
    {"BBC NEWS", PAGE_BBC},
    {"UK NEWS", PAGE_UK},
};
const int MENU_N = sizeof(items) / sizeof(items[0]);
int sel = 0;

// RSS cache
String warnTitles[10];
int warnCount = 0;
String bbcTitles[10];
int bbcCount = 0;
String ukTitles[10];
int ukCount = 0;
int rssScroll = 0;
int lastRssScroll = -999;

// Cork cache
struct CorkObs
{
    String timeStr;
    String report;
    int tempC = 0;
    int wind_kmh = 0;
    int gust_kmh = 0;
    String rain;
    int pressure = 0;
    bool ok = false;
} cork;

// ===================== THEME =====================
uint16_t UI_BG = ILI9341_BLACK;
uint16_t UI_GRID = ILI9341_DARKGREY;
uint16_t UI_PANEL = ILI9341_NAVY;
uint16_t UI_ACCENT = ILI9341_CYAN;
uint16_t UI_TEXT = ILI9341_WHITE;
uint16_t UI_DIM = ILI9341_DARKGREY;
uint16_t UI_HILITE = ILI9341_DARKCYAN;
uint16_t UI_NEON = ILI9341_GREEN;
uint16_t UI_WARN_Y = ILI9341_YELLOW;
uint16_t UI_WARN_O = ILI9341_ORANGE;
uint16_t UI_WARN_R = ILI9341_RED;

// ===================== UI HELPERS =====================
void uiCornerCuts(int x, int y, int w, int h, uint16_t col)
{
    tft.drawFastHLine(x, y, 12, col);
    tft.drawFastVLine(x, y, 12, col);
    tft.drawFastHLine(x + w - 12, y + h - 1, 12, col);
    tft.drawFastVLine(x + w - 1, y + h - 12, 12, col);
}

void uiSubtleGrid()
{
    for (int x = 0; x < 320; x += 40)
    {
        for (int y = 0; y < 240; y += 40)
        {
            tft.drawPixel(x, y, UI_GRID);
            tft.drawPixel(x + 1, y, UI_GRID);
        }
    }
    tft.drawLine(0, 210, 140, 120, UI_GRID);
    tft.drawLine(10, 210, 150, 120, UI_GRID);
}

String wifiBars()
{
    if (WiFi.status() != WL_CONNECTED)
        return "..";
    int rssi = WiFi.RSSI();
    if (rssi > -55)
        return "||||";
    if (rssi > -65)
        return "|||";
    if (rssi > -75)
        return "||";
    if (rssi > -85)
        return "|";
    return ".";
}

void uiHeader(const char *title)
{
    tft.fillRect(0, 0, 320, 28, UI_PANEL);
    tft.drawRect(0, 0, 320, 28, UI_ACCENT);
    uiCornerCuts(0, 0, 320, 28, UI_ACCENT);

    tft.setFont();
    tft.setCursor(10, 7);
    tft.setTextSize(2);
    tft.setTextColor(UI_TEXT);
    tft.print(title);

    tft.setTextSize(1);
    tft.setTextColor(UI_DIM);
    tft.setCursor(235, 10);
    tft.print("NET:");
    tft.print(wifiBars());
}

void uiFooter(const char *hint)
{
    tft.setFont();
    tft.fillRect(0, 226, 320, 14, UI_BG);
    tft.drawFastHLine(0, 226, 320, UI_GRID);
    tft.setTextSize(1);
    tft.setTextColor(UI_DIM);
    tft.setCursor(10, 228);
    tft.print(hint);
}

void uiCard(int x, int y, int w, int h, const char *label)
{
    tft.fillRect(x, y, w, h, UI_BG);
    tft.drawRect(x, y, w, h, UI_ACCENT);
    uiCornerCuts(x, y, w, h, UI_ACCENT);
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(UI_DIM);
    tft.setCursor(x + 8, y + 6);
    tft.print(label);
}

void uiDrawBackground(const char *title)
{
    tft.fillScreen(UI_BG);
    uiSubtleGrid();
    uiHeader(title);
}

void drawCenteredText(int y, const String &s, uint16_t col, int size)
{
    tft.setFont();
    tft.setTextSize(size);
    tft.setTextColor(col);

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(s, 0, y, &x1, &y1, &w, &h);

    int x = (320 - (int)w) / 2;
    tft.setCursor(x, y);
    tft.print(s);
}

int drawWrapped(const String &text, int x, int y, int maxW, int lineH, int maxLines)
{
    String line = "";
    int lines = 0;
    int i = 0;

    while (i < (int)text.length() && lines < maxLines)
    {
        while (i < (int)text.length() && text[i] == ' ')
            i++;
        int j = i;
        while (j < (int)text.length() && text[j] != ' ')
            j++;

        String word = text.substring(i, j);
        i = j;

        String tryLine = (line.length() == 0) ? word : (line + " " + word);

        int16_t x1, y1;
        uint16_t w, h;
        tft.getTextBounds(tryLine, 0, 0, &x1, &y1, &w, &h);

        if ((int)w <= maxW)
        {
            line = tryLine;
        }
        else
        {
            tft.setCursor(x, y + lines * lineH);
            tft.print(line);
            lines++;
            line = word;
        }
    }

    if (lines < maxLines && line.length() > 0)
    {
        tft.setCursor(x, y + lines * lineH);
        tft.print(line);
        lines++;
    }

    return lines;
}

// ===================== HOME CLOCK CYBER DIGITS =====================
const bool digitMap[10][7] = {
    {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1}, {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1}, {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 1, 1}};

void fillSegH(int x, int y, int w, int t, uint16_t col)
{
    tft.fillRect(x, y, w, t, col);
}

void fillSegV(int x, int y, int h, int t, uint16_t col)
{
    tft.fillRect(x, y, t, h, col);
}

void drawCyberDigit(int x, int y, int digit, uint16_t col, uint16_t glow)
{
    const int w = 48;
    const int h = 100;
    const int t = 9;
    int mid = y + h / 2 - t / 2;

    if (digitMap[digit][0])
        fillSegH(x + 1, y + 1, w, t, glow);
    if (digitMap[digit][1])
        fillSegV(x + w - t + 1, y + 4, h / 2 - 6, t, glow);
    if (digitMap[digit][2])
        fillSegV(x + w - t + 1, mid + 4, h / 2 - 6, t, glow);
    if (digitMap[digit][3])
        fillSegH(x + 1, y + h - t + 1, w, t, glow);
    if (digitMap[digit][4])
        fillSegV(x + 1, mid + 4, h / 2 - 6, t, glow);
    if (digitMap[digit][5])
        fillSegV(x + 1, y + 4, h / 2 - 6, t, glow);
    if (digitMap[digit][6])
        fillSegH(x + 1, mid + 1, w, t, glow);

    if (digitMap[digit][0])
        fillSegH(x, y, w, t, col);
    if (digitMap[digit][1])
        fillSegV(x + w - t, y + 4, h / 2 - 6, t, col);
    if (digitMap[digit][2])
        fillSegV(x + w - t, mid + 4, h / 2 - 6, t, col);
    if (digitMap[digit][3])
        fillSegH(x, y + h - t, w, t, col);
    if (digitMap[digit][4])
        fillSegV(x, mid + 4, h / 2 - 6, t, col);
    if (digitMap[digit][5])
        fillSegV(x, y + 4, h / 2 - 6, t, col);
    if (digitMap[digit][6])
        fillSegH(x, mid, w, t, col);
}

void drawCyberColon(int x, int y, uint16_t col, uint16_t glow)
{
    tft.fillCircle(x + 1, y + 25, 4, glow);
    tft.fillCircle(x + 1, y + 60, 4, glow);
    tft.fillCircle(x, y + 24, 3, col);
    tft.fillCircle(x, y + 59, 3, col);
}

void drawCyberTime(int x, int y, const String &s)
{
    const int digitW = 48;
    const int gap = 12;
    const int colonGap = 26;
    const int colonW = 14;

    uint16_t hourCol = tft.color565(255, 0, 150);
    uint16_t minCol = tft.color565(255, 80, 200);
    uint16_t glowCol = tft.color565(80, 0, 40);

    int cx = x;

    for (int i = 0; i < s.length(); i++)
    {
        char c = s[i];

        if (c >= '0' && c <= '9')
        {
            uint16_t col = (i < 2) ? hourCol : minCol;
            drawCyberDigit(cx, y, c - '0', col, glowCol);

            if (i == 1)
                cx += digitW + colonGap;
            else
                cx += digitW + gap;
        }
        else if (c == ':')
        {
            drawCyberColon(cx, y, ILI9341_CYAN, glowCol);
            cx += colonW + colonGap;
        }
    }
}

void drawRetroGrid()
{
    uint16_t pink = tft.color565(255, 0, 150);
    uint16_t cyan = tft.color565(0, 180, 255);

    int horizon = 120;

    for (int y = 230; y > horizon; y -= 8)
    {
        int fade = map(y, horizon, 230, 40, 255);
        uint16_t col = tft.color565(fade, 0, fade / 2);
        tft.drawFastHLine(0, y, 320, col);
    }

    for (int x = 0; x <= 320; x += 20)
    {
        tft.drawLine(x, 230, 160, horizon, pink);
    }

    tft.drawFastHLine(0, horizon, 320, cyan);
}

// ===================== BUZZER =====================
void buzzerBegin()
{
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
}

void playToneSafe(int freq, int durationMs)
{
    tone(BUZZER_PIN, freq, durationMs);
    delay(durationMs + 35);
    noTone(BUZZER_PIN);
}

void happyBeep()
{
    playToneSafe(1319, 110);
    playToneSafe(1568, 110);
    playToneSafe(1760, 110);
    playToneSafe(2093, 180);
    playToneSafe(1760, 90);
    playToneSafe(2093, 220);
    playToneSafe(2637, 260);
}

// ===================== LOVE MODE DRAW =====================
void drawLoveFace()
{
    tft.fillScreen(ILI9341_BLACK);
    uiHeader("LOVE MODE");
    uiFooter("Touched!");

    tft.fillCircle(95, 105, 30, ILI9341_CYAN);
    tft.fillCircle(225, 105, 30, ILI9341_CYAN);

    tft.fillCircle(84, 92, 8, ILI9341_WHITE);
    tft.fillCircle(214, 92, 8, ILI9341_WHITE);

    tft.fillCircle(70, 155, 7, ILI9341_MAGENTA);
    tft.fillCircle(250, 155, 7, ILI9341_MAGENTA);

    tft.drawLine(140, 175, 180, 175, ILI9341_MAGENTA);
    tft.drawLine(138, 173, 142, 177, ILI9341_MAGENTA);
    tft.drawLine(178, 177, 182, 173, ILI9341_MAGENTA);

    tft.setFont();
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(138, 195);
    tft.print("<3");
}

void triggerLoveMode()
{
    loveMode = true;
    loveModeUntil = millis() + 4000;

    fill_solid(leds, NUM_LEDS, CRGB(80, 10, 45));
    FastLED.show();

    drawLoveFace();
    happyBeep();
}

// ===================== LED EFFECTS =====================
// void updateLeds() {
//   if (loveMode) return;

//   unsigned long now = millis();
//   if (now - lastLedMs < 65) return;
//   lastLedMs = now;

//   static int pos = 0;

//   switch (currentEffect) {
//     case FX_CYBER: {
//       fxBreath += fxDir;
//       if (fxBreath >= 120 || fxBreath <= 30) fxDir = -fxDir;
//       fill_solid(leds, NUM_LEDS, CRGB(0, fxBreath, fxBreath));
//       break;
//     }

//     case FX_CALM: {
//       fadeToBlackBy(leds, NUM_LEDS, 40);
//       leds[pos] = CRGB(0, 200, 200);
//       pos++;
//       if (pos >= NUM_LEDS) pos = 0;
//       break;
//     }

//     case FX_RAINBOW: {
//       for (int i = 0; i < NUM_LEDS; i++) {
//         leds[i] = CHSV(fxHue + i * 6, 255, 70);
//       }
//       fxHue++;
//       break;
//     }

//     case FX_ALERT: {
//       for (int i = 0; i < NUM_LEDS; i++) {
//         int wave = sin8(i * 10 + fxHue);
//         leds[i] = CRGB(0, wave / 2, wave);
//       }
//       fxHue += 4;
//       break;
//     }

//     case FX_OFF:
//     default:
//       fill_solid(leds, NUM_LEDS, CRGB::Black);
//       break;
//   }

//   FastLED.show();
// }
void updateLeds()
{
    if (loveMode)
        return;

    unsigned long now = millis();
    if (now - lastLedMs < 65)
        return;
    lastLedMs = now;

    static int pos = 0;
    static uint8_t sparkleFade[NUM_LEDS] = {0};

    switch (currentEffect)
    {
    case FX_CYBER:
    {
        // neon cyberpunk: cyan + magenta moving pulse
        fadeToBlackBy(leds, NUM_LEDS, 28);

        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint8_t wave1 = sin8(i * 18 + fxHue);
            uint8_t wave2 = sin8(i * 24 - fxHue * 2);

            CRGB cyanGlow = CHSV(140, 220, scale8(wave1, 150));
            CRGB magentaGlow = CHSV(210, 180, scale8(wave2, 110));

            leds[i] += cyanGlow;
            leds[i] += magentaGlow;
        }

        // moving hot pixel
        leds[pos] += CRGB(255, 20, 120);
        if (pos > 0)
            leds[pos - 1] += CRGB(60, 0, 50);
        if (pos < NUM_LEDS - 1)
            leds[pos + 1] += CRGB(0, 40, 60);

        pos++;
        if (pos >= NUM_LEDS)
            pos = 0;

        fxHue += 3;
        break;
    }

    case FX_CALM:
    {
        // slower purple/pink ambient drift
        fadeToBlackBy(leds, NUM_LEDS, 18);

        for (int i = 0; i < NUM_LEDS; i++)
        {
            uint8_t wave = sin8(i * 10 + fxHue / 2);
            leds[i] += CHSV(210, 160, scale8(wave, 65)); // pink/purple
        }

        leds[pos] += CHSV(220, 120, 90);
        pos++;
        if (pos >= NUM_LEDS)
            pos = 0;

        fxHue += 1;
        break;
    }

    case FX_RAINBOW:
    {
        // brighter rainbow
        for (int i = 0; i < NUM_LEDS; i++)
        {
            leds[i] = CHSV(fxHue + i * 10, 255, 140);
        }
        fxHue += 2;
        break;
    }

    case FX_SEXY_RED:
    {
        // calm dim red with random sexy sparkles
        fadeToBlackBy(leds, NUM_LEDS, 22);

        for (int i = 0; i < NUM_LEDS; i++)
        {
            // dark red base glow
            leds[i] += CRGB(18, 0, 0);

            // sparkle decay
            if (sparkleFade[i] > 3)
                sparkleFade[i] -= 3;
            else
                sparkleFade[i] = 0;

            leds[i] += CRGB(sparkleFade[i], 0, sparkleFade[i] / 10);
        }

        // random pop
        if (random8() < 70)
        {
            int idx = random(NUM_LEDS);
            sparkleFade[idx] = random(120, 255);
        }

        // sometimes pop a nearby second LED
        if (random8() < 25)
        {
            int idx2 = random(NUM_LEDS);
            sparkleFade[idx2] = random(80, 180);
        }

        break;
    }

    case FX_LAMP_WARM:
    {
        fill_solid(leds, NUM_LEDS, CRGB(255, 140, 55));
        break;
    }

    case FX_LAMP_WHITE:
    {
        fill_solid(leds, NUM_LEDS, CRGB(255, 230, 180));
        break;
    }

    case FX_OFF:
    default:
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        break;
    }

    FastLED.show();
}
// ===================== NET =====================
void wifiConnect()
{
    if (WiFi.status() == WL_CONNECTED)
        return;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    wifiMulti.addAP(WIFI1_SSID, WIFI1_PASS);
    if (strlen(WIFI2_SSID) > 0)
        wifiMulti.addAP(WIFI2_SSID, WIFI2_PASS);

    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED && millis() - start < 12000)
    {
        delay(200);
    }
}

String httpsGET_follow(const char *url)
{
    wifiConnect();
    if (WiFi.status() != WL_CONNECTED)
        return "";

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(8000);

    if (!http.begin(client, url))
        return "";

    http.addHeader("Accept-Encoding", "identity");
    http.addHeader("User-Agent", "Mozilla/5.0 (ESP32)");

    int code = http.GET();
    if (code <= 0)
    {
        http.end();
        return "";
    }

    String body = http.getString();
    http.end();
    return body;
}

// ===================== RSS =====================
String stripCdata(String s)
{
    s.trim();
    if (s.startsWith("<![CDATA["))
    {
        s.remove(0, 9);
        int end = s.indexOf("]]>");
        if (end >= 0)
            s = s.substring(0, end);
    }
    s.trim();
    return s;
}

int parseRssTitles(const String &xml, String outTitles[], int maxTitles)
{
    int count = 0;
    int pos = 0;

    while (count < maxTitles)
    {
        int itemPos = xml.indexOf("<item", pos);
        if (itemPos < 0)
            break;

        int t1 = xml.indexOf("<title>", itemPos);
        if (t1 < 0)
            break;
        t1 += 7;
        int t2 = xml.indexOf("</title>", t1);
        if (t2 < 0)
            break;

        String title = stripCdata(xml.substring(t1, t2));
        title.replace("&amp;", "&");
        title.replace("&quot;", "\"");
        title.replace("&#39;", "'");
        title.replace("&apos;", "'");
        title.replace("&lt;", "<");
        title.replace("&gt;", ">");
        title.trim();

        outTitles[count++] = title;
        pos = t2 + 8;
    }

    return count;
}

void fetchWarnings()
{
    warnCount = 0;
    String xml = httpsGET_follow(URL_METWARN_RSS);
    if (xml.length() == 0)
        return;
    warnCount = parseRssTitles(xml, warnTitles, 6);
}

void fetchBBC()
{
    bbcCount = 0;
    String xml = httpsGET_follow(URL_BBC_RSS);
    if (xml.length() == 0)
        return;
    bbcCount = parseRssTitles(xml, bbcTitles, 6);
}

void fetchUK()
{
    ukCount = 0;
    String xml = httpsGET_follow(URL_UK_RSS);
    if (xml.length() == 0)
        return;
    ukCount = parseRssTitles(xml, ukTitles, 6);
}

// ===================== CORK CSV =====================
static bool splitCsvLine(const String &line, String cols[], int maxCols)
{
    int col = 0;
    bool inQ = false;
    String cur;

    for (int i = 0; i < (int)line.length(); i++)
    {
        char c = line[i];
        if (c == '\"')
            inQ = !inQ;
        else if (c == ',' && !inQ)
        {
            if (col < maxCols)
                cols[col++] = cur;
            cur = "";
        }
        else
        {
            cur += c;
        }
    }

    if (col < maxCols)
        cols[col++] = cur;
    return col >= 2;
}

void fetchCork()
{
    cork.ok = false;
    String csv = httpsGET_follow(URL_CORK_CSV);
    if (csv.length() == 0)
        return;

    int end = csv.length() - 1;
    while (end > 0 && (csv[end] == '\n' || csv[end] == '\r' || csv[end] == ' '))
        end--;
    int start = csv.lastIndexOf('\n', end);
    if (start < 0)
        start = 0;

    String lastLine = csv.substring(start);
    lastLine.trim();

    if (lastLine.startsWith("Time") || lastLine.indexOf(',') < 0)
    {
        int end2 = start - 1;
        if (end2 > 0)
        {
            int start2 = csv.lastIndexOf('\n', end2);
            if (start2 < 0)
                start2 = 0;
            lastLine = csv.substring(start2, end2);
            lastLine.trim();
        }
    }

    String cols[12];
    if (!splitCsvLine(lastLine, cols, 12))
        return;

    cork.timeStr = cols[0];
    cork.report = cols[1];
    cork.tempC = cols[2].toInt();
    cork.wind_kmh = cols[3].toInt();
    cork.gust_kmh = cols[4].toInt();
    cork.rain = cols[6];
    cork.pressure = cols[7].toInt();
    cork.ok = true;
}

// ===================== CLOCK =====================
void setupNTP()
{
    wifiConnect();
    if (WiFi.status() != WL_CONNECTED)
        return;
    configTzTime("GMT0BST,M3.5.0/1,M10.5.0/2", "pool.ntp.org", "time.google.com");
}

bool timeReady()
{
    time_t now = time(nullptr);
    return now > 1700000000;
}

String nowHHMM()
{
    if (!timeReady())
        return "--:--";
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return String(buf);
}

String nowDateStr()
{
    if (!timeReady())
        return "---";
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%a %d %b %Y", &tm);
    return String(buf);
}

// ===================== WARN COLORS =====================
uint16_t warnColorForTitle(const String &t)
{
    String s = t;
    s.toLowerCase();
    if (s.indexOf("status red") >= 0 || s.indexOf(" red") >= 0)
        return UI_WARN_R;
    if (s.indexOf("status orange") >= 0 || s.indexOf(" orange") >= 0)
        return UI_WARN_O;
    if (s.indexOf("status yellow") >= 0 || s.indexOf(" yellow") >= 0)
        return UI_WARN_Y;
    return UI_ACCENT;
}

// ===================== DRAW PAGES =====================
void drawMenuStatic()
{
    uiDrawBackground("CYBER MENU");
    uiFooter("UP/DOWN | SELECT");
    uiCard(10, 36, 300, 184, "APPS");
}

void drawMenuDynamic()
{
    tft.setFont();
    tft.setTextSize(2);

    for (int i = 0; i < MENU_N; i++)
    {
        int y = 54 + i * 24;
        tft.fillRect(18, y - 2, 284, 22, UI_BG);

        if (i == sel)
        {
            tft.fillRect(18, y - 2, 284, 22, UI_HILITE);
            tft.setTextColor(UI_TEXT);
        }
        else
        {
            tft.setTextColor(ILI9341_LIGHTGREY);
        }

        tft.setCursor(22, y);
        tft.print(i == sel ? "> " : "  ");
        tft.print(items[i].label);
    }
}

void drawHomeStatic()
{
    tft.fillScreen(ILI9341_BLACK);
    drawRetroGrid();

    tft.drawFastHLine(0, 226, 320, ILI9341_DARKGREY);
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(120, 229);
    tft.print("MENU");
}

void updateHomeDynamic()
{
    tft.fillRect(0, 40, 320, 170, ILI9341_BLACK);
    drawRetroGrid();

    String t = nowHHMM();
    int totalW = 4 * 48 + 2 * 12 + 2 * 26 + 14;
    int startX = (320 - totalW) / 2 - 20;
    int startY = 70;

    drawCyberTime(startX, startY, t);

    tft.setFont();
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(70, 200);
    tft.print(nowDateStr());
}

void drawLightsStatic()
{
    uiDrawBackground("LIGHTS");
    uiFooter("UP/DOWN change | SELECT back");
    uiCard(10, 36, 300, 150, "EFFECTS");
}

void updateLightsDynamic()
{
    tft.fillRect(18, 56, 284, 118, UI_BG);

    for (int i = 0; i < FX_COUNT; i++)
    {
        int y = 64 + i * 20;

        if (i == currentEffect)
        {
            tft.fillRect(22, y - 2, 270, 18, UI_HILITE);
            tft.setTextColor(UI_TEXT);
        }
        else
        {
            tft.setTextColor(ILI9341_LIGHTGREY);
        }

        tft.setFont();
        tft.setTextSize(2);
        tft.setCursor(26, y);
        tft.print(i == currentEffect ? "> " : "  ");
        tft.print(effectNames[i]);
    }
}

void drawLocalStatic()
{
    uiDrawBackground("LOCAL SENSOR");
    uiFooter("SELECT = Back");

    uiCard(10, 36, 300, 54, "TEMP");
    uiCard(10, 96, 300, 54, "HUMIDITY");
    uiCard(10, 156, 300, 54, "PRESSURE");
}

void updateLocalDynamic()
{
    float temp = bme.readTemperature();
    float humidity = bme.readHumidity();
    float pressure = bme.readPressure() / 100.0;

    tft.fillRect(18, 56, 284, 28, UI_BG);
    tft.fillRect(18, 116, 284, 28, UI_BG);
    tft.fillRect(18, 176, 284, 28, UI_BG);

    tft.setFont();
    tft.setTextSize(3);

    // TEMP
    tft.setTextColor(UI_WARN_Y);
    tft.setCursor(20, 60);
    if (isnan(temp))
    {
        tft.print("ERR");
    }
    else
    {
        tft.print(temp, 1);
        tft.print(" C");
    }

    // HUMIDITY
    tft.setTextColor(UI_ACCENT);
    tft.setCursor(20, 120);
    if (isnan(humidity))
    {
        tft.print("ERR");
    }
    else
    {
        tft.print(humidity, 1);
        tft.print(" %");
    }

    // PRESSURE
    tft.setTextColor(UI_TEXT);
    tft.setCursor(20, 180);
    if (isnan(pressure) || pressure <= 0)
    {
        tft.print("ERR");
    }
    else
    {
        tft.print(pressure, 0);
        tft.print(" hPa");
    }
}

void drawCorkStatic()
{
    uiDrawBackground("CORK AIRPORT");
    uiFooter("SELECT = Back");
    uiCard(10, 36, 300, 74, "NOW");
    uiCard(10, 116, 300, 104, "DETAILS");
}

void updateCorkDynamic()
{
    tft.fillRect(12, 40, 296, 180, UI_BG);

    if (WiFi.status() != WL_CONNECTED)
    {
        drawCenteredText(95, "WiFi OFF", UI_WARN_R, 3);
        return;
    }

    if (!cork.ok)
    {
        tft.setFont();
        tft.setTextSize(2);
        tft.setTextColor(UI_WARN_R);
        tft.setCursor(20, 80);
        tft.print("Download failed.");
        tft.setCursor(20, 105);
        tft.setTextColor(UI_TEXT);
        tft.print("Open page again.");
        return;
    }

    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(UI_DIM);
    tft.setCursor(20, 52);
    tft.print("Updated: ");
    tft.setTextColor(UI_WARN_Y);
    tft.print(cork.timeStr);

    tft.setTextSize(4);
    tft.setTextColor(UI_WARN_Y);
    tft.setCursor(20, 70);
    tft.print(cork.tempC);
    tft.print("C");

    tft.setTextSize(2);
    tft.setTextColor(UI_TEXT);
    tft.setCursor(20, 132);
    tft.print("Sky: ");
    tft.print(cork.report);
    tft.setCursor(20, 154);
    tft.print("Wind: ");
    tft.print(cork.wind_kmh);
    tft.print("km/h");
    tft.setCursor(20, 176);
    tft.print("Rain: ");
    tft.print(cork.rain);
    tft.setCursor(20, 198);
    tft.print("Pres: ");
    tft.print(cork.pressure);
    tft.print("hPa");
}

void drawRSSStatic(const char *title)
{
    uiDrawBackground(title);
    uiFooter("UP/DOWN scroll | SELECT back");
}

void updateRSSDynamic(String titles[], int count, int scroll, bool warnColors)
{
    tft.fillRect(0, 28, 320, 198, UI_BG);

    if (WiFi.status() != WL_CONNECTED)
    {
        drawCenteredText(95, "WiFi OFF", UI_WARN_R, 3);
        return;
    }

    if (count == 0)
    {
        drawCenteredText(95, "No items", UI_WARN_R, 2);
        return;
    }

    int y = 36;
    int shown = 0;

    tft.setFont(&FreeSans9pt7b);

    for (int i = scroll; i < count && shown < 3; i++)
    {
        uint16_t col = warnColors ? warnColorForTitle(titles[i]) : UI_ACCENT;

        tft.drawRect(8, y, 304, 58, UI_GRID);
        tft.fillRect(8, y, 5, 58, col);
        tft.fillCircle(22, y + 14, 3, col);

        tft.setTextColor(UI_TEXT);
        drawWrapped(titles[i], 32, y + 14, 268, 16, 3);

        y += 64;
        shown++;
    }

    tft.setFont();
}

// ===================== PAGE FLOW =====================
void enterPage(Page p)
{
    page = p;
    lastDrawnPage = (Page)255;
}

void drawStaticIfNeeded()
{
    if (page == lastDrawnPage)
        return;

    if (page == PAGE_MENU)
    {
        drawMenuStatic();
        drawMenuDynamic();
    }
    if (page == PAGE_HOME)
    {
        drawHomeStatic();
        updateHomeDynamic();
    }
    if (page == PAGE_LIGHTS)
    {
        drawLightsStatic();
        updateLightsDynamic();
    }
    if (page == PAGE_LOCAL)
    {
        drawLocalStatic();
        updateLocalDynamic();
    }
    if (page == PAGE_CORK)
    {
        drawCorkStatic();
        updateCorkDynamic();
    }

    if (page == PAGE_WARN)
    {
        drawRSSStatic("MET WARNINGS");
        lastRssScroll = -999;
    }
    if (page == PAGE_BBC)
    {
        drawRSSStatic("BBC NEWS");
        lastRssScroll = -999;
    }
    if (page == PAGE_UK)
    {
        drawRSSStatic("BBC UK");
        lastRssScroll = -999;
    }

    lastDrawnPage = page;
}

void redrawCurrentPage()
{
    lastDrawnPage = (Page)255;
    drawStaticIfNeeded();

    if (page == PAGE_WARN)
        updateRSSDynamic(warnTitles, warnCount, rssScroll, true);
    if (page == PAGE_BBC)
        updateRSSDynamic(bbcTitles, bbcCount, rssScroll, false);
    if (page == PAGE_UK)
        updateRSSDynamic(ukTitles, ukCount, rssScroll, false);
}

// ===================== TOUCH =====================
bool rawTouchPressed()
{
    int v = digitalRead(TOUCH_PIN);
    if (TOUCH_ACTIVE_HIGH)
        return (v == HIGH);
    return (v == LOW);
}

bool updateTouchState()
{
    bool raw = rawTouchPressed();
    unsigned long now = millis();

    if (raw != touchRawLast)
    {
        touchRawLast = raw;
        touchLastChangeMs = now;
    }

    if ((now - touchLastChangeMs) > 35)
    {
        touchStable = raw;
    }

    return touchStable;
}

// ===================== SETUP / LOOP =====================
void setup()
{
    Serial.begin(115200);

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_SEL, INPUT_PULLUP);

    pinMode(TOUCH_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    buzzerBegin();

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(90);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    tft.begin();
    tft.setRotation(3);
    tft.setSPISpeed(20000000);

    Wire.begin(21, 22);

    if (!bme.begin(0x76))
    {
        Serial.println("BME280 not found at 0x76");
    }
    else
    {
        Serial.println("BME280 OK at 0x76");
    }

    uiDrawBackground("BOOT");
    drawCenteredText(90, "CONNECTING...", UI_WARN_Y, 2);
    uiFooter("Please wait");

    wifiConnect();
    setupNTP();

    if (WiFi.status() == WL_CONNECTED)
    {
        drawCenteredText(130, "ONLINE", UI_DIM, 2);
    }
    else
    {
        drawCenteredText(130, "OFFLINE", UI_WARN_R, 2);
    }

    ignoreTouchUntil = millis() + 1500;

    delay(500);
    enterPage(PAGE_HOME);
    drawStaticIfNeeded();
}

void loop()
{
    updateLeds();

    bool touchPressed = updateTouchState();

    if (millis() > ignoreTouchUntil)
    {
        static bool lastTouchPressed = false;
        if (touchPressed && !lastTouchPressed && !loveMode)
        {
            triggerLoveMode();
        }
        lastTouchPressed = touchPressed;
    }

    if (loveMode)
    {
        if (millis() > loveModeUntil)
        {
            loveMode = false;
            redrawCurrentPage();
        }
        return;
    }

    bool up = btnPressed(BTN_UP, lastUpMs);
    bool down = btnPressed(BTN_DOWN, lastDownMs);
    bool selb = btnPressed(BTN_SEL, lastSelMs);

    drawStaticIfNeeded();

    if (page == PAGE_MENU)
    {
        if (up)
        {
            sel--;
            if (sel < 0)
                sel = MENU_N - 1;
            drawMenuDynamic();
        }
        if (down)
        {
            sel++;
            if (sel >= MENU_N)
                sel = 0;
            drawMenuDynamic();
        }

        if (selb)
        {
            Page target = items[sel].target;
            rssScroll = 0;
            lastRssScroll = -999;

            if (target == PAGE_CORK)
                fetchCork();
            if (target == PAGE_WARN)
                fetchWarnings();
            if (target == PAGE_BBC)
                fetchBBC();
            if (target == PAGE_UK)
                fetchUK();
            if (target == PAGE_HOME)
                setupNTP();

            enterPage(target);
            drawStaticIfNeeded();

            if (page == PAGE_WARN)
                updateRSSDynamic(warnTitles, warnCount, rssScroll, true);
            if (page == PAGE_BBC)
                updateRSSDynamic(bbcTitles, bbcCount, rssScroll, false);
            if (page == PAGE_UK)
                updateRSSDynamic(ukTitles, ukCount, rssScroll, false);
        }
        return;
    }

    if (page == PAGE_LIGHTS)
    {
        if (up)
        {
            currentEffect--;
            if (currentEffect < 0)
                currentEffect = FX_COUNT - 1;
            updateLightsDynamic();
        }
        if (down)
        {
            currentEffect++;
            if (currentEffect >= FX_COUNT)
                currentEffect = 0;
            updateLightsDynamic();
        }
        if (selb)
        {
            enterPage(PAGE_MENU);
            drawStaticIfNeeded();
        }
        return;
    }

    if (selb)
    {
        enterPage(PAGE_MENU);
        drawStaticIfNeeded();
        return;
    }

    if (page == PAGE_WARN || page == PAGE_BBC || page == PAGE_UK)
    {
        int count = (page == PAGE_WARN) ? warnCount : (page == PAGE_BBC ? bbcCount : ukCount);

        if (up && count > 0)
        {
            rssScroll--;
            if (rssScroll < 0)
                rssScroll = 0;
        }

        if (down && count > 0)
        {
            rssScroll++;
            if (rssScroll > count - 1)
                rssScroll = count - 1;
        }

        if (rssScroll != lastRssScroll)
        {
            lastRssScroll = rssScroll;
            if (page == PAGE_WARN)
                updateRSSDynamic(warnTitles, warnCount, rssScroll, true);
            if (page == PAGE_BBC)
                updateRSSDynamic(bbcTitles, bbcCount, rssScroll, false);
            if (page == PAGE_UK)
                updateRSSDynamic(ukTitles, ukCount, rssScroll, false);
        }
    }

    static String lastHHMM = "";
    static String lastDate = "";
    static unsigned long lastPoll = 0;

    if (page == PAGE_HOME && millis() - lastPoll > 250)
    {
        lastPoll = millis();

        String hhmm = nowHHMM();
        String date = nowDateStr();

        if (hhmm != lastHHMM || date != lastDate)
        {
            lastHHMM = hhmm;
            lastDate = date;
            updateHomeDynamic();
        }
    }

    static unsigned long lastLocal = 0;
    if (page == PAGE_LOCAL && millis() - lastLocal > 3000)
    {
        lastLocal = millis();
        updateLocalDynamic();
    }
}