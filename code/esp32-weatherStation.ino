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
    PAGE_LOCAL,
    PAGE_CORK,
    PAGE_WARN,
    PAGE_BBC,
    PAGE_UK,
    PAGE_HOME
};

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DHT.h>
#include <time.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// ===================== WIFI (EDIT THIS) =====================
WiFiMulti wifiMulti;

static const char *WIFI1_SSID = "iPhone";
static const char *WIFI1_PASS = "victoria";

static const char *WIFI2_SSID = "";
static const char *WIFI2_PASS = "";

// ===================== URLS =====================
static const char *URL_CORK_CSV = "https://www.met.ie/latest-reports/observations/download/cork";
static const char *URL_METWARN_RSS = "https://www.met.ie/warningsxml/rss.xml";
static const char *URL_BBC_RSS = "https://feeds.bbci.co.uk/news/rss.xml";
static const char *URL_UK_RSS = "https://feeds.bbci.co.uk/news/uk/rss.xml";
// ===================== TFT =====================
// NOTE: keep your wiring/pins as-is (you said it works)
#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ===================== DHT22 =====================
#define DHTPIN 27
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

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
    {"LOCAL (DHT22)", PAGE_LOCAL},
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

// ===================== UI PRIMITIVES =====================
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

void uiHeader(const char *title)
{
    tft.fillRect(0, 0, 320, 28, UI_PANEL);
    tft.drawRect(0, 0, 320, 28, UI_ACCENT);
    uiCornerCuts(0, 0, 320, 28, UI_ACCENT);

    tft.setCursor(10, 7);
    tft.setTextSize(2);
    tft.setTextColor(UI_TEXT);
    tft.print(title);

    tft.setTextSize(1);
    tft.setTextColor(UI_DIM);
    tft.setCursor(250, 10);
    tft.print("NET:");
    tft.print(WiFi.status() == WL_CONNECTED ? "OK" : "OFF");
}

void uiFooter(const char *hint)
{
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

// ===================== TEXT HELPERS =====================
void drawCenteredText(int y, const String &s, uint16_t col, int size)
{
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
            line = tryLine;
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
    while (wifiMulti.run() != WL_CONNECTED && millis() - start < 15000)
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

    Serial.println("UK feed bytes:");
    Serial.println(xml.length());

    if (xml.length() == 0)
    {
        Serial.println("UK feed download failed");
        return;
    }

    ukCount = parseRssTitles(xml, ukTitles, 6);

    Serial.println("UK parsed items:");
    Serial.println(ukCount);
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
            cur += c;
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

// ===================== CLOCK (Ireland) =====================
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

// ===================== WARNING COLORS =====================
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

// ===================== PAGE DRAW (STATIC + DYNAMIC) =====================
void drawMenuStatic()
{
    uiDrawBackground("CYBER MENU");
    uiFooter("UP/DOWN | SELECT");
    uiCard(10, 36, 300, 184, "APPS");
}

void drawMenuDynamic()
{
    tft.setTextSize(2);
    for (int i = 0; i < MENU_N; i++)
    {
        int y = 54 + i * 26;
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
    tft.fillScreen(ILI9341_BLACK); // simple fast background
    uiHeader("HOME");
    uiFooter("SELECT = Menu");
    uiCard(10, 190, 300, 30, "STATUS");
    tft.setTextSize(1);
    tft.setTextColor(UI_DIM);
    tft.setCursor(20, 206);
    tft.print("UP/DOWN navigate | SELECT open/back");
}

void updateHomeDynamic()
{
    // center panel
    tft.fillRect(0, 52, 320, 100, ILI9341_BLACK);
    tft.drawFastHLine(35, 52, 250, ILI9341_CYAN);
    tft.drawFastHLine(35, 165, 250, ILI9341_CYAN);

    // nicer clock font
    tft.setFont(&FreeSansBold24pt7b);
    tft.setTextColor(ILI9341_CYAN);

    String t = nowHHMM();

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);

    int x = (320 - w) / 2;

    tft.setCursor(x, 130);
    tft.print(t);

    tft.setFont(); // return to default font

    // date
    drawCenteredText(150, nowDateStr(), ILI9341_LIGHTGREY, 2);
}

void drawLocalStatic()
{
    uiDrawBackground("LOCAL WEATHER");
    uiFooter("SELECT = Back");
    uiCard(10, 36, 300, 86, "TEMP");
    uiCard(10, 128, 300, 92, "HUMID");
}

void updateLocalDynamic()
{
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    tft.fillRect(18, 62, 284, 56, UI_BG);
    tft.fillRect(18, 154, 284, 56, UI_BG);

    tft.setTextSize(4);
    tft.setTextColor(UI_WARN_Y);
    tft.setCursor(20, 70);
    if (isnan(temp))
        tft.print("ERR");
    else
    {
        tft.print(temp, 1);
        tft.print("C");
    }

    tft.setTextSize(4);
    tft.setTextColor(UI_ACCENT);
    tft.setCursor(20, 164);
    if (isnan(hum))
        tft.print("ERR");
    else
    {
        tft.print(hum, 0);
        tft.print("%");
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
        tft.setTextSize(2);
        tft.setTextColor(UI_WARN_R);
        tft.setCursor(20, 80);
        tft.print("Download failed.");
        tft.setCursor(20, 105);
        tft.setTextColor(UI_TEXT);
        tft.print("Open page again.");
        return;
    }

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

// ===================== PAGE ENTER + STATIC DRAW =====================
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

// ===================== SETUP / LOOP =====================
void setup()
{
    Serial.begin(115200);

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_SEL, INPUT_PULLUP);

    tft.begin();
    tft.setRotation(3); // flip/rotate (change 1/3 if needed)
    tft.setSPISpeed(20000000);

    dht.begin();

    uiDrawBackground("BOOT");
    drawCenteredText(90, "CONNECTING...", UI_WARN_Y, 2);
    uiFooter("Please wait");

    wifiConnect();
    setupNTP();

    if (WiFi.status() == WL_CONNECTED)
    {
        drawCenteredText(130, "ONLINE", UI_DIM, 2);
    }

    enterPage(PAGE_HOME);
    drawStaticIfNeeded();
}

void loop()
{
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

    // HOME: no flicker (update only when minute/date changes)
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