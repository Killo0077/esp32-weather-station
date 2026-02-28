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

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DHT.h>
#include <time.h>

// ===================== WIFI (EDIT THIS) =====================
WiFiMulti wifiMulti;

// Add your hotspots here:
static const char *WIFI1_SSID = "iPhone";
static const char *WIFI1_PASS = "victoria";

// Example second hotspot (edit or delete):
static const char *WIFI2_SSID = "HerHotspotName";
static const char *WIFI2_PASS = "HerPassword";

// ===================== URLS =====================
static const char *URL_CORK_CSV = "https://www.met.ie/latest-reports/observations/download/cork";
static const char *URL_BBC_RSS = "https://feeds.bbci.co.uk/news/rss.xml";
static const char *URL_UK_RSS = "https://www.theguardian.com/uk-news/rss";

// ===================== TFT =====================
#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ===================== DHT22 =====================
#define DHTPIN 27
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ===================== JOYSTICK =====================
#define JOY_Y 35
#define JOY_SW 32
const int JOY_UP_TH = 1200;
const int JOY_DOWN_TH = 2800;

unsigned long lastMoveMs = 0;
unsigned long lastClickMs = 0;

// ===================== UI =====================
enum Page
{
    PAGE_MENU,
    PAGE_LOCAL,
    PAGE_CORK,
    PAGE_BBC,
    PAGE_UK,
    PAGE_CLOCK
};
Page page = PAGE_MENU;

struct Item
{
    const char *label;
    Page target;
};
Item items[] = {
    {"Local (DHT22)", PAGE_LOCAL},
    {"Cork Airport", PAGE_CORK},
    {"BBC News", PAGE_BBC},
    {"UK News", PAGE_UK},
    {"Clock", PAGE_CLOCK},
};
const int MENU_N = sizeof(items) / sizeof(items[0]);
int sel = 0;

// RSS cache
String bbcTitles[8];
int bbcCount = 0;
String ukTitles[8];
int ukCount = 0;
int rssScroll = 0;

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

// ===================== INPUT HELPERS =====================
bool clickPressed()
{
    if (digitalRead(JOY_SW) == LOW)
    {
        unsigned long now = millis();
        if (now - lastClickMs > 250)
        {
            lastClickMs = now;
            return true;
        }
    }
    return false;
}

int joyMove()
{
    unsigned long now = millis();
    if (now - lastMoveMs < 160)
        return 0;
    int y = analogRead(JOY_Y);
    if (y < JOY_UP_TH)
    {
        lastMoveMs = now;
        return -1;
    }
    if (y > JOY_DOWN_TH)
    {
        lastMoveMs = now;
        return +1;
    }
    return 0;
}

// ===================== DRAW HELPERS (320x240) =====================
void drawHeader(const char *title)
{
    tft.fillRect(0, 0, 320, 26, ILI9341_NAVY);
    tft.setCursor(8, 6);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_WHITE);
    tft.print(title);

    tft.setTextSize(1);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(250, 9);
    tft.print("NET:");
    tft.print(WiFi.status() == WL_CONNECTED ? "OK" : "OFF");
}

void drawFooter(const char *hint)
{
    tft.fillRect(0, 226, 320, 14, ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(8, 228);
    tft.print(hint);
}

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

// ===================== WIFI + HTTP =====================
void wifiConnect()
{
    if (WiFi.status() == WL_CONNECTED)
        return;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    // Add APs (safe to call again; WiFiMulti keeps list)
    wifiMulti.addAP(WIFI1_SSID, WIFI1_PASS);
    if (strlen(WIFI2_SSID) > 0)
        wifiMulti.addAP(WIFI2_SSID, WIFI2_PASS);

    unsigned long start = millis();
    while (wifiMulti.run() != WL_CONNECTED && millis() - start < 15000)
    {
        delay(300);
    }
}

String httpsGET_follow(const char *url)
{
    wifiConnect();
    if (WiFi.status() != WL_CONNECTED)
        return "";

    WiFiClientSecure client;
    client.setInsecure(); // simplest; works well on hotspots

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url))
        return "";

    // Avoid gzip (often breaks simple parsers)
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

// ===================== RSS PARSE =====================
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

        String title = xml.substring(t1, t2);
        title = stripCdata(title);

        title.replace("&amp;", "&");
        title.replace("&quot;", "\"");
        title.replace("&#39;", "'");
        title.replace("&apos;", "'");
        title.replace("&lt;", "<");
        title.replace("&gt;", ">");
        title.trim();

        // Some feeds have the channel title as first <item> title; keep anyway (fine)
        outTitles[count++] = title;
        pos = t2 + 8;
    }
    return count;
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

// ===================== CORK CSV PARSE =====================
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

    // last non-empty line
    int end = csv.length() - 1;
    while (end > 0 && (csv[end] == '\n' || csv[end] == '\r' || csv[end] == ' '))
        end--;
    int start = csv.lastIndexOf('\n', end);
    if (start < 0)
        start = 0;
    String lastLine = csv.substring(start);
    lastLine.trim();

    // If header, try previous line
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

    // Expected:
    // Time,Report,Temperature,wind_speed_km_h,wind_gust_km_h,Wind Direction º,rainfall_mm_h,Pressure hPa
    cork.timeStr = cols[0];
    cork.report = cols[1];
    cork.tempC = cols[2].toInt();
    cork.wind_kmh = cols[3].toInt();
    cork.gust_kmh = cols[4].toInt();
    cork.rain = cols[6];
    cork.pressure = cols[7].toInt();
    cork.ok = true;
}

// ===================== NTP CLOCK (IRELAND LOCAL TIME) =====================
void setupNTP()
{
    wifiConnect();
    if (WiFi.status() != WL_CONNECTED)
        return;

    // Ireland local time (DST aware)
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

// ===================== PAGES =====================
void drawMenu()
{
    tft.fillScreen(ILI9341_BLACK);
    drawHeader("MENU");
    drawFooter("UP/DOWN select | CLICK open");

    tft.setTextSize(2);
    for (int i = 0; i < MENU_N; i++)
    {
        int y = 44 + i * 34;
        if (i == sel)
        {
            tft.fillRect(10, y - 4, 300, 28, ILI9341_DARKCYAN);
            tft.setTextColor(ILI9341_WHITE);
        }
        else
        {
            tft.drawRect(10, y - 4, 300, 28, ILI9341_DARKGREY);
            tft.setTextColor(ILI9341_LIGHTGREY);
        }
        tft.setCursor(18, y);
        tft.print(i == sel ? "> " : "  ");
        tft.print(items[i].label);
    }
}

void drawLocal()
{
    tft.fillScreen(ILI9341_BLACK);
    drawHeader("LOCAL WEATHER");
    drawFooter("CLICK = Back");

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);
    tft.setCursor(10, 50);
    tft.print("Temp: ");
    if (isnan(temp))
        tft.print("ERR");
    else
    {
        tft.print(temp, 1);
        tft.print(" C");
    }

    tft.setCursor(10, 95);
    tft.print("Hum : ");
    if (isnan(hum))
        tft.print("ERR");
    else
    {
        tft.print(hum, 0);
        tft.print(" %");
    }

    tft.setTextSize(1);
    tft.setTextColor(ILI9341_DARKGREY);
    tft.setCursor(10, 160);
    tft.print("Next: nicer UI with icons.");
}

void drawCork()
{
    tft.fillScreen(ILI9341_BLACK);
    drawHeader("CORK AIRPORT");
    drawFooter("CLICK = Back");

    if (WiFi.status() != WL_CONNECTED)
    {
        drawCenteredText(90, "WiFi OFF", ILI9341_RED, 3);
        return;
    }

    if (!cork.ok)
    {
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_RED);
        tft.setCursor(10, 60);
        tft.print("Download failed.");
        tft.setTextColor(ILI9341_WHITE);
        tft.setCursor(10, 90);
        tft.print("Try again (open page).");
        return;
    }

    tft.setTextSize(1);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(10, 30);
    tft.print("Updated: ");
    tft.print(cork.timeStr);

    tft.setTextSize(3);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(10, 55);
    tft.print(cork.tempC);
    tft.print(" C");

    tft.setTextSize(2);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 100);
    tft.print("Sky: ");
    tft.print(cork.report);

    tft.setCursor(10, 130);
    tft.print("Wind: ");
    tft.print(cork.wind_kmh);
    tft.print(" km/h");

    tft.setCursor(10, 155);
    tft.print("Gust: ");
    tft.print(cork.gust_kmh);

    tft.setCursor(10, 180);
    tft.print("Rain: ");
    tft.print(cork.rain);

    tft.setCursor(10, 205);
    tft.print("Pres: ");
    tft.print(cork.pressure);
    tft.print(" hPa");
}

void drawRSS(const char *title, String titles[], int count, int scroll)
{
    tft.fillScreen(ILI9341_BLACK);
    drawHeader(title);
    drawFooter("UP/DOWN scroll | CLICK back");

    if (WiFi.status() != WL_CONNECTED)
    {
        drawCenteredText(90, "WiFi OFF", ILI9341_RED, 3);
        return;
    }
    if (count == 0)
    {
        drawCenteredText(90, "No headlines", ILI9341_RED, 2);
        return;
    }

    tft.setTextSize(2);
    int y = 34;
    int shown = 0;
    for (int i = scroll; i < count && shown < 3; i++)
    {
        tft.setTextColor(ILI9341_CYAN);
        tft.setCursor(10, y);
        tft.print("\x95 ");

        tft.setTextColor(ILI9341_WHITE);
        int lines = drawWrapped(titles[i], 30, y, 280, 18, 3);
        y += lines * 18 + 10;
        shown++;
    }
}

void drawClock()
{
    tft.fillScreen(ILI9341_BLACK);
    drawHeader("IRELAND TIME");
    drawFooter("CLICK = Back");

    if (WiFi.status() != WL_CONNECTED)
    {
        drawCenteredText(90, "WiFi OFF", ILI9341_RED, 3);
        tft.setTextSize(1);
        tft.setTextColor(ILI9341_DARKGREY);
        tft.setCursor(10, 140);
        tft.print("Turn on hotspot for NTP time.");
        return;
    }

    if (!timeReady())
    {
        drawCenteredText(90, "Syncing time...", ILI9341_YELLOW, 2);
        return;
    }

    drawCenteredText(70, nowHHMM(), ILI9341_GREEN, 6);
    drawCenteredText(150, nowDateStr(), ILI9341_WHITE, 2);
}

// ===================== RENDER =====================
void render()
{
    if (page == PAGE_MENU)
        drawMenu();
    if (page == PAGE_LOCAL)
        drawLocal();
    if (page == PAGE_CORK)
        drawCork();
    if (page == PAGE_BBC)
        drawRSS("BBC NEWS", bbcTitles, bbcCount, rssScroll);
    if (page == PAGE_UK)
        drawRSS("UK NEWS", ukTitles, ukCount, rssScroll);
    if (page == PAGE_CLOCK)
        drawClock();
}

// ===================== SETUP / LOOP =====================
void setup()
{
    pinMode(JOY_SW, INPUT_PULLUP);

    tft.begin();
    tft.setRotation(1); // HORIZONTAL (320x240)

    dht.begin();

    render(); // show menu quickly

    wifiConnect();
    setupNTP();

    if (WiFi.status() == WL_CONNECTED)
    {
        fetchCork();
        fetchBBC();
        fetchUK();
    }

    render();
}

void loop()
{
    if (page == PAGE_MENU)
    {
        int mv = joyMove();
        if (mv != 0)
        {
            sel += mv;
            if (sel < 0)
                sel = MENU_N - 1;
            if (sel >= MENU_N)
                sel = 0;
            drawMenu();
        }
        if (clickPressed())
        {
            page = items[sel].target;
            rssScroll = 0;

            if (page == PAGE_CORK)
                fetchCork();
            if (page == PAGE_BBC)
                fetchBBC();
            if (page == PAGE_UK)
                fetchUK();
            if (page == PAGE_CLOCK)
                setupNTP();

            render();
        }
    }
    else
    {
        // RSS scroll
        if (page == PAGE_BBC || page == PAGE_UK)
        {
            int mv = joyMove();
            int count = (page == PAGE_BBC) ? bbcCount : ukCount;
            if (mv != 0 && count > 0)
            {
                rssScroll += mv;
                if (rssScroll < 0)
                    rssScroll = 0;
                if (rssScroll > count - 1)
                    rssScroll = count - 1;
                render();
            }
        }

        if (clickPressed())
        {
            page = PAGE_MENU;
            render();
        }

        static unsigned long lastClock = 0;
        if (page == PAGE_CLOCK && millis() - lastClock > 1000)
        {
            lastClock = millis();
            drawClock();
        }

        static unsigned long lastLocal = 0;
        if (page == PAGE_LOCAL && millis() - lastLocal > 3000)
        {
            lastLocal = millis();
            drawLocal();
        }
    }
}