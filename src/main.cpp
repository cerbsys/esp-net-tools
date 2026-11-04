
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <time.h>

#include "config.h"
#include <lwip/sockets.h>
#include "hardware/display.h"
#include "ui/colors.h"
#include "ui/splash.h"

enum class Screen {
    HOME,
    SCAN,
    DETAILS,
    SETUP,
    TOOLS,
    KEYBOARD,
    TRACE_RESULTS,
    LAN_DISCOVERY,
    SPEED_TEST,
    PORT_RESULTS
};

enum class ToolInput {
    NONE,
    DNS,
    HTTP,
    PORT,
    PING,
    TRACE
};

Screen currentScreen = Screen::HOME;
bool needsRedraw = true;
bool touchLatched = false;
bool setupPortalActive = false;
bool mdnsStarted = false;
unsigned long lastRefresh = 0;
constexpr uint8_t WIFI_VISIBLE_ROWS = 4;
uint16_t wifiScrollOffset = 0;
String wifiSsid;
String wifiPassword;
String setupSsid;
String setupPassword;
String toolTitle = "Choose a check";
String toolResult;
String inputValue;
String traceTarget;
String traceHops[8];
uint8_t traceHopCount = 0;
String portScanTarget;
String portScanStatus;
String portResults[6];
uint8_t portResultCount = 0;
struct LanDevice {
    IPAddress address;
    uint16_t responseMs;
    char hostname[16];
    char manufacturer[16];
};
constexpr uint16_t LAN_MAX_HOSTS = 254;
constexpr uint8_t LAN_VISIBLE_ROWS = 4;
enum LanScanState : uint8_t {
    LAN_SCAN_IDLE,
    LAN_SCAN_RUNNING,
    LAN_SCAN_ENRICHING,
    LAN_SCAN_COMPLETE,
    LAN_SCAN_OFFLINE,
    LAN_SCAN_TOO_LARGE,
    LAN_SCAN_ICMP_UNAVAILABLE,
    LAN_SCAN_TASK_FAILED
};
enum class SpeedTestState : uint8_t {
    IDLE,
    RUNNING,
    COMPLETE,
    FAILED,
    OFFLINE
};
enum class SpeedTestStage : uint8_t {
    CLOCK,
    LATENCY,
    DOWNLOAD,
    UPLOAD
};
LanDevice lanDevices[LAN_MAX_HOSTS];
volatile uint16_t lanDeviceCount = 0;
volatile uint16_t lanScanProgress = 0;
volatile uint16_t lanScanTotal = 0;
volatile LanScanState lanScanState = LAN_SCAN_IDLE;
portMUX_TYPE lanScanMux = portMUX_INITIALIZER_UNLOCKED;
uint16_t lanScrollOffset = 0;
uint32_t lanNetworkStart = 0;
uint32_t lanLocalAddress = 0;
uint16_t lanHostCount = 0;
portMUX_TYPE speedTestMux = portMUX_INITIALIZER_UNLOCKED;
volatile SpeedTestState speedTestState = SpeedTestState::IDLE;
volatile SpeedTestStage speedTestStage = SpeedTestStage::CLOCK;
volatile int32_t speedLatencyMs = -1;
volatile int32_t speedDownloadCentiMbps = -1;
volatile int32_t speedUploadCentiMbps = -1;
ToolInput inputMode = ToolInput::NONE;
Preferences preferences;
WebServer server(80);

bool hasWifiCredentials() {
    return wifiSsid.length() > 0;
}

void fillArea(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color) {
    tft.fillRect(x, y, width, height, color);
}

void drawText(const String &text, int16_t x, int16_t y, uint16_t color, uint8_t font = 1) {
    tft.setTextColor(color);
    tft.drawString(text, x, y, font);
}

void drawHeader() {
    fillArea(0, 0, DISPLAY_WIDTH, 34, Colors::PANEL);
    drawText("CERBSYS", 12, 12, Colors::ACCENT);
    drawText("NET TOOLS", 78, 12, Colors::WHITE);
}

void drawFooter() {
    fillArea(0, 214, DISPLAY_WIDTH, 26, Colors::PANEL);
    drawText("HOME", 34, 223, currentScreen == Screen::HOME ? Colors::ACCENT : Colors::GRAY);
    drawText("TOOLS", 132, 223,
        currentScreen == Screen::TOOLS || currentScreen == Screen::SCAN ||
            currentScreen == Screen::TRACE_RESULTS || currentScreen == Screen::LAN_DISCOVERY ||
            currentScreen == Screen::SPEED_TEST || currentScreen == Screen::PORT_RESULTS
            ? Colors::ACCENT : Colors::GRAY);
    drawText("NETWORK", 225, 223, currentScreen == Screen::DETAILS ? Colors::ACCENT : Colors::GRAY);
}

void drawHome() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Device status", 12, 46, Colors::WHITE, 2);
    drawText("WI-FI", 14, 80, Colors::GRAY);

    if (WiFi.status() == WL_CONNECTED) {
        drawText("CONNECTED", 14, 96, Colors::GREEN);
        drawText(WiFi.localIP().toString(), 14, 113, Colors::WHITE);
    } else {
        drawText(hasWifiCredentials() ? "CONNECTING" : "NOT CONFIGURED", 14, 96, Colors::YELLOW);
        drawText("Scanning is available offline", 14, 113, Colors::GRAY);
    }

    tft.drawRoundRect(8, 145, 96, 52, 5, Colors::ACCENT);
    tft.drawRoundRect(112, 145, 96, 52, 5, Colors::GRAY);
    tft.drawRoundRect(216, 145, 96, 52, 5, Colors::GRAY);
    drawText("WI-FI", 36, 159, Colors::ACCENT);
    drawText("SETUP", 34, 176, Colors::ACCENT);
    drawText("WI-FI", 140, 159, Colors::WHITE);
    drawText("INFO", 139, 176, Colors::WHITE);
    drawText("NET", 252, 159, Colors::WHITE);
    drawText("TOOLS", 246, 176, Colors::WHITE);
}

void drawScan() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Nearby Wi-Fi networks", 12, 43, Colors::WHITE);

    const int16_t scanResult = WiFi.scanComplete();
    if (scanResult == WIFI_SCAN_RUNNING) {
        drawText("Scanning...", 12, 72, Colors::YELLOW);
    } else if (scanResult == WIFI_SCAN_FAILED) {
        drawText("Tap SCAN to start", 12, 72, Colors::GRAY);
    } else if (scanResult == 0) {
        drawText("No networks found", 12, 72, Colors::GRAY);
    } else {
            const uint16_t total = static_cast<uint16_t>(scanResult);
            const uint16_t maxOffset = total > WIFI_VISIBLE_ROWS ? total - WIFI_VISIBLE_ROWS : 0;
            if (wifiScrollOffset > maxOffset) wifiScrollOffset = maxOffset;
            for (uint8_t row = 0; row < WIFI_VISIBLE_ROWS; ++row) {
                const uint16_t index = wifiScrollOffset + row;
                if (index >= total) break;
                const int16_t y = 66 + row * 31;
            tft.drawFastHLine(10, y + 24, 300, Colors::PANEL);
            String ssid = WiFi.SSID(index);
            if (ssid.length() == 0) {
                ssid = "<hidden network>";
            }
            drawText(ssid.substring(0, 24), 12, y, Colors::WHITE);

            const String details = "CH " + String(WiFi.channel(index)) + "  " +
                String(WiFi.RSSI(index)) + " dBm";
            drawText(details, 12, y + 13, Colors::GRAY);
            drawText(WiFi.encryptionType(index) == WIFI_AUTH_OPEN ? "OPEN" : "SECURE",
                244, y + 13, Colors::GRAY);
        }

            const int16_t trackTop = 66;
            const int16_t trackHeight = 130;
            tft.drawRoundRect(302, trackTop, 10, trackHeight, 4, Colors::GRAY);
            uint16_t thumbHeight = trackHeight;
            if (total > WIFI_VISIBLE_ROWS) {
                thumbHeight = (trackHeight * WIFI_VISIBLE_ROWS) / total;
                if (thumbHeight < 18) thumbHeight = 18;
            }
            const uint16_t thumbRange = trackHeight - thumbHeight;
            const uint16_t thumbTop = trackTop + (maxOffset == 0 ? 0 :
                (wifiScrollOffset * thumbRange) / maxOffset);
            tft.fillRoundRect(304, thumbTop + 1, 6, thumbHeight - 2, 3, Colors::ACCENT);
    }

    tft.drawRoundRect(254, 39, 56, 23, 4, Colors::ACCENT);
    drawText("SCAN", 264, 47, Colors::ACCENT);
}

void drawNetworkDetails() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Connection details", 12, 43, Colors::WHITE);

    const bool connected = WiFi.status() == WL_CONNECTED;
    const char *state = connected ? "CONNECTED" : "DISCONNECTED";
    drawText(state, 224, 43, connected ? Colors::GREEN : Colors::YELLOW);

    const IPAddress emptyAddress(0, 0, 0, 0);
    const IPAddress local = connected ? WiFi.localIP() : emptyAddress;
    const IPAddress gateway = connected ? WiFi.gatewayIP() : emptyAddress;
    const IPAddress subnet = connected ? WiFi.subnetMask() : emptyAddress;
    const IPAddress dns = connected ? WiFi.dnsIP() : emptyAddress;

    const char *labels[] = {"IP", "GATEWAY", "SUBNET", "DNS", "SIGNAL"};
    const String values[] = {
        local.toString(), gateway.toString(), subnet.toString(), dns.toString(),
        connected ? String(WiFi.RSSI()) + " dBm" : "--"
    };

    for (uint8_t index = 0; index < 5; ++index) {
        const int16_t y = 64 + index * 20;
        drawText(labels[index], 14, y, Colors::GRAY);
        drawText(values[index], 92, y, Colors::WHITE);
        tft.drawFastHLine(12, y + 16, 296, Colors::PANEL);
    }

    const char *action = connected ? "DISCONNECT" : hasWifiCredentials() ? "CONNECT" : "SETUP";
    tft.drawRoundRect(188, 177, 122, 27, 4, connected ? Colors::RED : Colors::ACCENT);
    drawText(action, 214, 186, connected ? Colors::RED : Colors::ACCENT);
}

void drawSetup() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Connect this device", 12, 43, Colors::WHITE, 2);
    drawText("Join this Wi-Fi from your phone:", 12, 76, Colors::GRAY);
    drawText("NETWORK", 12, 98, Colors::GRAY);
    drawText(setupSsid, 102, 98, Colors::ACCENT);
    drawText("PASSWORD", 12, 120, Colors::GRAY);
    drawText(setupPassword, 102, 120, Colors::ACCENT);
    drawText("Then open 192.168.4.1", 12, 151, Colors::WHITE);
    drawText("Connected phones: " + String(WiFi.softAPgetStationNum()), 12, 177, Colors::GRAY);
}

void drawTools() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Network checks", 12, 43, Colors::WHITE, 2);

    const int16_t buttonY[] = {60, 93, 126};
    for (uint8_t row = 0; row < 3; ++row) {
        tft.drawRoundRect(8, buttonY[row], 145, 28, 4, Colors::ACCENT);
        tft.drawRoundRect(165, buttonY[row], 145, 28, 4, Colors::ACCENT);
    }
    drawText("DNS LOOKUP", 44, 69, Colors::ACCENT);
    drawText("HTTP CHECK", 199, 69, Colors::ACCENT);
    drawText("WI-FI SCAN", 43, 102, Colors::ACCENT);
    drawText("PORT SCAN", 209, 102, Colors::ACCENT);
    drawText("PING", 67, 135, Colors::ACCENT);
    drawText("TRACE", 221, 135, Colors::ACCENT);
    tft.drawRoundRect(8, 159, 145, 28, 4, Colors::ACCENT);
    drawText("LAN DISCOVERY", 41, 168, Colors::ACCENT);
    tft.drawRoundRect(165, 159, 145, 28, 4, Colors::ACCENT);
    drawText("SPEED TEST", 207, 168, Colors::ACCENT);
    drawText(toolResult.substring(0, 34), 12, 195, Colors::GREEN);
    if (WiFi.status() != WL_CONNECTED) {
        drawText("OFFLINE", 246, 195, Colors::YELLOW);
    }
}

String formatSpeedRate(int32_t hundredthsMbps) {
    if (hundredthsMbps < 0) return "N/A";
    return String(hundredthsMbps / 100) + "." +
        (hundredthsMbps % 100 < 10 ? "0" : "") + String(hundredthsMbps % 100) + " Mbps";
}

void drawSpeedTest() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Internet speed test", 12, 43, Colors::WHITE, 2);
    drawText("Cloudflare test endpoint", 12, 64, Colors::GRAY);

    SpeedTestState state;
    SpeedTestStage stage;
    int32_t latency;
    int32_t download;
    int32_t upload;
    portENTER_CRITICAL(&speedTestMux);
    state = speedTestState;
    stage = speedTestStage;
    latency = speedLatencyMs;
    download = speedDownloadCentiMbps;
    upload = speedUploadCentiMbps;
    portEXIT_CRITICAL(&speedTestMux);

    drawText("Latency", 14, 91, Colors::GRAY);
    drawText(latency < 0 ? "N/A" : String(latency) + " ms", 150, 91, Colors::WHITE);
    drawText("Download", 14, 116, Colors::GRAY);
    drawText(formatSpeedRate(download), 150, 116, Colors::WHITE);
    drawText("Upload", 14, 141, Colors::GRAY);
    drawText(formatSpeedRate(upload), 150, 141, Colors::WHITE);

    const char *stageText = stage == SpeedTestStage::CLOCK ? "Syncing clock" :
        stage == SpeedTestStage::LATENCY ? "Measuring latency" :
        stage == SpeedTestStage::DOWNLOAD ? "Downloading test data" : "Uploading test data";
    const char *status = state == SpeedTestState::RUNNING ? stageText :
        state == SpeedTestState::COMPLETE ? "Test complete" :
        state == SpeedTestState::OFFLINE ? "Connect to Wi-Fi first" :
        state == SpeedTestState::FAILED ? "Test failed; tap to retry" : "Ready";
    drawText(status, 12, 168, state == SpeedTestState::RUNNING ? Colors::YELLOW : Colors::GRAY);
    tft.drawRoundRect(211, 180, 101, 28, 4, Colors::ACCENT);
    drawText("RUN TEST", 228, 189, Colors::ACCENT);
}

void drawLanDiscovery() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("LAN devices", 12, 39, Colors::WHITE, 2);

    LanScanState state;
    uint16_t deviceCount;
    uint16_t progress;
    uint16_t total;
    uint16_t scrollOffset;
    portENTER_CRITICAL(&lanScanMux);
    state = lanScanState;
    deviceCount = lanDeviceCount;
    progress = lanScanProgress;
    total = lanScanTotal;
    scrollOffset = lanScrollOffset;
    portEXIT_CRITICAL(&lanScanMux);

    if (state == LAN_SCAN_OFFLINE) {
        drawText("Connect to a private Wi-Fi network", 12, 72, Colors::YELLOW);
        return;
    }
    if (state == LAN_SCAN_TOO_LARGE) {
        drawText("Subnet exceeds 254 hosts; scan stopped", 12, 72, Colors::YELLOW);
        return;
    }
    if (state == LAN_SCAN_ICMP_UNAVAILABLE) {
        drawText("ICMP discovery is unavailable", 12, 72, Colors::YELLOW);
        return;
    }
    if (state == LAN_SCAN_TASK_FAILED) {
        drawText("Could not start LAN scan", 12, 72, Colors::YELLOW);
        return;
    }
    if (state == LAN_SCAN_IDLE) {
        drawText("Open from Net Tools to start", 12, 72, Colors::GRAY);
        return;
    }

    String status = "Complete  Found " + String(deviceCount) + "/" + String(total);
    uint16_t statusColor = Colors::GREEN;
    if (state == LAN_SCAN_RUNNING) {
        status = "Scanning " + String(progress) + "/" + String(total) +
            "  Found " + String(deviceCount);
        statusColor = Colors::YELLOW;
    } else if (state == LAN_SCAN_ENRICHING) {
        status = "Looking up device details...";
        statusColor = Colors::YELLOW;
    }
    drawText(status, 12, 59, statusColor);

    const uint16_t visibleRows = LAN_VISIBLE_ROWS;
    const uint16_t maxOffset = deviceCount > visibleRows ? deviceCount - visibleRows : 0;
    if (scrollOffset > maxOffset) {
        scrollOffset = maxOffset;
        lanScrollOffset = maxOffset;
    }

    LanDevice visibleDevices[LAN_VISIBLE_ROWS] = {};
    portENTER_CRITICAL(&lanScanMux);
    for (uint16_t row = 0; row < visibleRows; ++row) {
        const uint16_t deviceIndex = scrollOffset + row;
        if (deviceIndex < lanDeviceCount) {
            visibleDevices[row] = lanDevices[deviceIndex];
        }
    }
    portEXIT_CRITICAL(&lanScanMux);

    const uint16_t trackTop = 77;
    const uint16_t trackHeight = 120;
    tft.drawRoundRect(302, trackTop, 10, trackHeight, 4, Colors::GRAY);
    uint16_t thumbHeight = trackHeight;
    if (deviceCount > visibleRows) {
        thumbHeight = (trackHeight * visibleRows) / deviceCount;
        if (thumbHeight < 18) thumbHeight = 18;
    }
    const uint16_t thumbRange = trackHeight - thumbHeight;
    const uint16_t thumbTop = trackTop + (maxOffset == 0 ? 0 :
        (scrollOffset * thumbRange) / maxOffset);
    tft.fillRoundRect(304, thumbTop + 1, 6, thumbHeight - 2, 3, Colors::ACCENT);

    for (uint16_t row = 0; row < visibleRows; ++row) {
        const uint16_t deviceIndex = scrollOffset + row;
        if (deviceIndex >= deviceCount) break;
        const int16_t y = 78 + row * 30;
        const String hostname = visibleDevices[row].hostname[0]
            ? String(visibleDevices[row].hostname) : "hostname unknown";
        const String manufacturer = visibleDevices[row].manufacturer[0]
            ? String(visibleDevices[row].manufacturer) : "manufacturer unknown";
        drawText(String(deviceIndex + 1) + " " + visibleDevices[row].address.toString() +
            "  " + String(visibleDevices[row].responseMs) + "ms", 12, y, Colors::ACCENT);
        drawText(hostname.substring(0, 15) + "  " + manufacturer.substring(0, 15),
            20, y + 14, Colors::WHITE);
    }
}

void drawTraceResults() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Trace: " + traceTarget.substring(0, 26), 12, 43, Colors::WHITE);
    if (traceHopCount == 0) {
        drawText("Trace unavailable", 12, 72, Colors::YELLOW);
        return;
    }
    for (uint8_t index = 0; index < traceHopCount; ++index) {
        drawText(String(index + 1) + ": " + traceHops[index], 12,
            66 + index * 17, Colors::GREEN);
    }
}

void drawPortResults() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    drawText("Port results: " + portScanTarget, 12, 43, Colors::WHITE);
    if (portScanStatus.length() > 0) {
        drawText(portScanStatus, 12, 72, Colors::YELLOW);
        return;
    }
    if (portResultCount == 0) {
        drawText("No common TCP ports open", 12, 72, Colors::GRAY);
        return;
    }
    for (uint8_t index = 0; index < portResultCount; ++index) {
        drawText(portResults[index].substring(0, 47), 12, 67 + index * 21,
            Colors::GREEN);
    }
}

void drawKeyboardRow(const char *keys, int16_t y) {
    const int16_t length = strlen(keys);
    const int16_t cellWidth = 29;
    const int16_t startX = (DISPLAY_WIDTH - length * cellWidth + 3) / 2;
    for (int16_t index = 0; index < length; ++index) {
        const int16_t x = startX + index * cellWidth;
        tft.drawRoundRect(x, y, 26, 21, 3, Colors::GRAY);
        drawText(String(keys[index]), x + 9, y + 6, Colors::WHITE);
    }
}

void drawInput() {
    fillArea(0, 34, DISPLAY_WIDTH, 180, Colors::BACKGROUND);
    const char *title = inputMode == ToolInput::DNS ? "DNS hostname" :
        inputMode == ToolInput::HTTP ? "HTTP hostname" :
        inputMode == ToolInput::PORT ? "Private LAN IPv4" : "Ping/trace destination";
    drawText(title, 12, 42, Colors::WHITE);
    tft.drawRoundRect(10, 62, 300, 27, 4, Colors::ACCENT);
    drawText(inputValue.substring(inputValue.length() > 32 ? inputValue.length() - 32 : 0),
        16, 70, Colors::WHITE);

    drawKeyboardRow("1234567890", 94);
    drawKeyboardRow("qwertyuiop", 118);
    drawKeyboardRow("asdfghjkl", 142);
    drawKeyboardRow("zxcvbnm.-", 166);

    tft.drawRoundRect(10, 191, 88, 20, 3, Colors::GRAY);
    tft.drawRoundRect(106, 191, 88, 20, 3, Colors::GRAY);
    tft.drawRoundRect(222, 191, 88, 20, 3, Colors::ACCENT);
    drawText("BACK", 35, 197, Colors::GRAY);
    drawText("DELETE", 126, 197, Colors::GRAY);
    drawText("RUN", 251, 197, Colors::ACCENT);
}

void setupWebServer() {
    server.on("/", HTTP_GET, []() {
        static const char page[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cerbsys Wi-Fi setup</title><style>
body{font:16px sans-serif;max-width:420px;margin:32px auto;padding:0 18px;background:#101820;color:#f2f5f7}
label{display:block;margin:18px 0 6px}input{box-sizing:border-box;width:100%;padding:12px;border:1px solid #71808b;border-radius:5px;font-size:16px}
button{margin-top:20px;padding:12px 18px;background:#00a8b5;color:#fff;border:0;border-radius:5px;font-size:16px}
</style></head><body><h2>Cerbsys Net Tools</h2><p>Enter the Wi-Fi network this device should join.</p>
<form method="post" action="/save"><label for="ssid">Network name</label>
<input id="ssid" name="ssid" maxlength="32" autocomplete="off" required>
<label for="password">Password</label><input id="password" name="password" type="password" maxlength="63" autocomplete="new-password">
<button type="submit">Save and connect</button></form></body></html>)rawliteral";
        server.send_P(200, "text/html", page);
    });

    server.on("/save", HTTP_POST, []() {
        const String ssid = server.arg("ssid");
        const String password = server.arg("password");
        if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) {
            server.send(400, "text/plain", "Invalid network name or password length.");
            return;
        }

        preferences.begin("nettools", false);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.end();

        wifiSsid = ssid;
        wifiPassword = password;
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
        server.send(200, "text/html",
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<h2>Saved</h2><p>Cerbsys is connecting. You can close this page.</p>");
    });

    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    });
    server.begin();
}

void startSetupPortal() {
    if (!setupPortalActive) {
        String suffix = WiFi.macAddress().substring(9);
        suffix.replace(":", "");
        suffix.toUpperCase();
        setupSsid = "Cerbsys-" + suffix;
        setupPassword = "CT" + suffix;

        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(setupSsid.c_str(), setupPassword.c_str());
        setupWebServer();
        setupPortalActive = true;
    }
    currentScreen = Screen::SETUP;
    needsRedraw = true;
}

void stopSetupPortal() {
    if (!setupPortalActive) {
        return;
    }
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    setupPortalActive = false;
    if (hasWifiCredentials() && WiFi.status() != WL_CONNECTED) {
        WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }
}

void runDnsCheck(const String &host) {
    toolTitle = "DNS: " + host;
    if (WiFi.status() != WL_CONNECTED) {
        toolResult = "Not connected";
    } else {
        IPAddress address;
        const unsigned long started = millis();
        if (WiFi.hostByName(host.c_str(), address)) {
            toolResult = address.toString() + "  " + String(millis() - started) + " ms";
        } else {
            toolResult = "Lookup failed";
        }
    }
    currentScreen = Screen::TOOLS;
    needsRedraw = true;
}

void runHttpCheck(const String &host) {
    toolTitle = "HTTP: " + host;
    if (WiFi.status() != WL_CONNECTED) {
        toolResult = "Not connected";
    } else {
        IPAddress address;
        WiFiClient client;
        client.setTimeout(4000);
        const unsigned long started = millis();
        if (!WiFi.hostByName(host.c_str(), address)) {
            toolResult = "DNS lookup failed";
        } else if (!client.connect(address, 80)) {
            toolResult = "Internet connection failed";
        } else {
            client.print("HEAD / HTTP/1.1\r\nHost: ");
            client.print(host);
            client.print("\r\nConnection: close\r\n\r\n");
            const String response = client.readStringUntil('\n');
            client.stop();
            toolResult = response.startsWith("HTTP/")
                ? String(millis() - started) + " ms  " + response.substring(0, 18)
                : "No HTTP response";
        }
    }
    currentScreen = Screen::TOOLS;
    needsRedraw = true;
}

bool isPrivateAddress(const IPAddress &address) {
    const uint8_t first = address[0];
    const uint8_t second = address[1];
    return first == 10 || first == 127 ||
        (first == 172 && second >= 16 && second <= 31) ||
        (first == 192 && second == 168);
}

String serviceNameForPort(uint16_t port) {
    switch (port) {
        case 22: return "SSH";
        case 53: return "DNS/TCP";
        case 80: return "HTTP";
        case 443: return "HTTPS";
        case 445: return "SMB";
        case 8080: return "HTTP-alt";
        default: return "unknown";
    }
}

String readPortServiceDetail(WiFiClient &client, uint16_t port) {
    client.setTimeout(180);
    if (port == 22) {
        String banner = client.readStringUntil('\n');
        banner.trim();
        return banner.startsWith("SSH-") ? " " + banner.substring(0, 20) : "";
    }
    if (port == 80 || port == 8080) {
        client.print("HEAD / HTTP/1.0\r\nConnection: close\r\n\r\n");
        String status = client.readStringUntil('\n');
        status.trim();
        if (!status.startsWith("HTTP/")) return "";
        String detail = " " + status.substring(0, 20);
        for (uint8_t header = 0; header < 4 && client.connected(); ++header) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) break;
            String lowerLine = line;
            lowerLine.toLowerCase();
            if (lowerLine.startsWith("server:")) {
                detail += " " + line.substring(7, 23);
                break;
            }
        }
        return detail;
    }
    return "";
}

void runPortCheck(const String &target) {
    portScanTarget = target;
    portScanStatus = "";
    portResultCount = 0;
    IPAddress address;
    if (!address.fromString(target) || !isPrivateAddress(address)) {
        portScanStatus = "Use a private LAN IPv4 address";
    } else if (WiFi.status() != WL_CONNECTED) {
        portScanStatus = "Not connected";
    } else {
        static const uint16_t ports[] = {22, 53, 80, 443, 445, 8080};
        for (uint8_t index = 0; index < sizeof(ports) / sizeof(ports[0]); ++index) {
            WiFiClient client;
            if (client.connect(address, ports[index], 250)) {
                const String service = serviceNameForPort(ports[index]);
                const String detail = readPortServiceDetail(client, ports[index]);
                if (portResultCount < sizeof(ports) / sizeof(ports[0])) {
                    portResults[portResultCount++] = String(ports[index]) + "/" + service + detail;
                }
                client.stop();
            }
        }
        if (portResultCount == 0) {
            portScanStatus = "No common TCP ports open";
        }
    }
    currentScreen = Screen::PORT_RESULTS;
    needsRedraw = true;
}

bool resolveNetworkTarget(const String &host, IPAddress &address) {
    return address.fromString(host) || WiFi.hostByName(host.c_str(), address);
}

void setSocketAddress(sockaddr_in &socketAddress, const IPAddress &address, uint16_t port) {
    memset(&socketAddress, 0, sizeof(socketAddress));
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(port);
    const uint8_t octets[] = {address[0], address[1], address[2], address[3]};
    memcpy(&socketAddress.sin_addr.s_addr, octets, sizeof(octets));
}

uint16_t icmpChecksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    for (size_t index = 0; index + 1 < length; index += 2) {
        sum += (static_cast<uint16_t>(data[index]) << 8) | data[index + 1];
    }
    if (length & 1) {
        sum += static_cast<uint16_t>(data[length - 1]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

bool resolveIcmpOffset(const uint8_t *packet, size_t length, size_t &offset) {
    offset = 0;
    if (length >= 20 && (packet[0] >> 4) == 4) {
        offset = static_cast<size_t>(packet[0] & 0x0F) * 4;
    }
    return length >= offset + 8;
}

uint32_t ipToInteger(const IPAddress &address) {
    return (static_cast<uint32_t>(address[0]) << 24) |
        (static_cast<uint32_t>(address[1]) << 16) |
        (static_cast<uint32_t>(address[2]) << 8) | address[3];
}

IPAddress integerToIp(uint32_t address) {
    return IPAddress(static_cast<uint8_t>(address >> 24),
        static_cast<uint8_t>(address >> 16), static_cast<uint8_t>(address >> 8),
        static_cast<uint8_t>(address));
}

bool pingAddress(int socketFd, const IPAddress &destination, uint16_t identifier,
    uint16_t sequence, uint16_t &responseMs) {
    uint8_t packet[8] = {8, 0, 0, 0, 0, 0, 0, 0};
    packet[4] = static_cast<uint8_t>(identifier >> 8);
    packet[5] = static_cast<uint8_t>(identifier);
    packet[6] = static_cast<uint8_t>(sequence >> 8);
    packet[7] = static_cast<uint8_t>(sequence);
    const uint16_t checksum = icmpChecksum(packet, sizeof(packet));
    packet[2] = static_cast<uint8_t>(checksum >> 8);
    packet[3] = static_cast<uint8_t>(checksum);

    sockaddr_in targetAddress;
    setSocketAddress(targetAddress, destination, 0);
    const unsigned long started = millis();
    if (sendto(socketFd, packet, sizeof(packet), 0,
        reinterpret_cast<sockaddr *>(&targetAddress), sizeof(targetAddress)) != sizeof(packet)) {
        return false;
    }

    uint8_t response[96];
    sockaddr_in responder = {};
    socklen_t responderLength = sizeof(responder);
    const ssize_t received = recvfrom(socketFd, response, sizeof(response), 0,
        reinterpret_cast<sockaddr *>(&responder), &responderLength);
    size_t icmpOffset = 0;
    if (received <= 0 || !resolveIcmpOffset(response, received, icmpOffset) ||
        response[icmpOffset] != 0 || response[icmpOffset + 1] != 0 ||
        response[icmpOffset + 4] != packet[4] || response[icmpOffset + 5] != packet[5] ||
        response[icmpOffset + 6] != packet[6] || response[icmpOffset + 7] != packet[7] ||
        responder.sin_addr.s_addr != targetAddress.sin_addr.s_addr) {
        return false;
    }

    responseMs = static_cast<uint16_t>(millis() - started);
    return true;
}

void runPingCheck(const String &host) {
    toolTitle = "Ping: " + host;
    toolResult.clear();
    if (WiFi.status() != WL_CONNECTED) {
        toolResult = "Not connected";
    } else {
        IPAddress destination;
        if (!resolveNetworkTarget(host, destination)) {
            toolResult = "Host lookup failed";
        } else {
            const int socketFd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
            if (socketFd < 0) {
                toolResult = "ICMP socket unavailable";
            } else {
                timeval timeout = {0, 700000};
                setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

                uint8_t packet[8] = {8, 0, 0, 0, 0, 0, 0, 1};
                const uint16_t identifier = static_cast<uint16_t>(ESP.getEfuseMac());
                packet[4] = static_cast<uint8_t>(identifier >> 8);
                packet[5] = static_cast<uint8_t>(identifier);
                const uint16_t checksum = icmpChecksum(packet, sizeof(packet));
                packet[2] = static_cast<uint8_t>(checksum >> 8);
                packet[3] = static_cast<uint8_t>(checksum);

                sockaddr_in targetAddress;
                setSocketAddress(targetAddress, destination, 0);
                const unsigned long started = millis();
                const ssize_t sent = sendto(socketFd, packet, sizeof(packet), 0,
                    reinterpret_cast<sockaddr *>(&targetAddress), sizeof(targetAddress));
                uint8_t response[96];
                sockaddr_in responder = {};
                socklen_t responderLength = sizeof(responder);
                const ssize_t received = sent == sizeof(packet)
                    ? recvfrom(socketFd, response, sizeof(response), 0,
                        reinterpret_cast<sockaddr *>(&responder), &responderLength)
                    : -1;

                size_t icmpOffset = 0;
                if (received > 0 && resolveIcmpOffset(response, received, icmpOffset) &&
                    response[icmpOffset] == 0 && response[icmpOffset + 1] == 0 &&
                    response[icmpOffset + 4] == packet[4] &&
                    response[icmpOffset + 5] == packet[5]) {
                    const uint8_t *octets = reinterpret_cast<const uint8_t *>(
                        &responder.sin_addr.s_addr);
                    const IPAddress replyAddress(octets[0], octets[1], octets[2], octets[3]);
                    toolResult = replyAddress.toString() + "  " + String(millis() - started) + " ms";
                } else {
                    toolResult = "Request timed out";
                }
                closesocket(socketFd);
            }
        }
    }
    currentScreen = Screen::TOOLS;
    needsRedraw = true;
}

void runTraceCheck(const String &host) {
    traceTarget = host;
    traceHopCount = 0;
    toolTitle = "Trace: " + host;
    toolResult = "Max 8 hops";

    if (WiFi.status() != WL_CONNECTED) {
        toolResult = "Not connected";
    } else {
        IPAddress destination;
        if (!resolveNetworkTarget(host, destination)) {
            toolResult = "Host lookup failed";
        } else {
            const int udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            const int icmpSocket = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
            if (udpSocket < 0 || icmpSocket < 0) {
                toolResult = "Trace sockets unavailable";
                if (udpSocket >= 0) closesocket(udpSocket);
                if (icmpSocket >= 0) closesocket(icmpSocket);
            } else {
                timeval timeout = {0, 350000};
                setsockopt(icmpSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                uint8_t payload[8] = {'C', 'E', 'R', 'B', 'S', 'Y', 'S', 0};
                bool reachedDestination = false;

                for (uint8_t ttl = 1; ttl <= 8 && !reachedDestination; ++ttl) {
                    const int hopLimit = ttl;
                    if (setsockopt(udpSocket, IPPROTO_IP, IP_TTL, &hopLimit, sizeof(hopLimit)) < 0) {
                        traceHops[traceHopCount++] = "TTL unsupported";
                        break;
                    }

                    sockaddr_in targetAddress;
                    setSocketAddress(targetAddress, destination, 33434 + ttl);
                    payload[7] = ttl;
                    const ssize_t sent = sendto(udpSocket, payload, sizeof(payload), 0,
                        reinterpret_cast<sockaddr *>(&targetAddress), sizeof(targetAddress));
                    if (sent != sizeof(payload)) {
                        traceHops[traceHopCount++] = "Send failed";
                        break;
                    }

                    uint8_t response[128];
                    sockaddr_in responder = {};
                    socklen_t responderLength = sizeof(responder);
                    const ssize_t received = recvfrom(icmpSocket, response, sizeof(response), 0,
                        reinterpret_cast<sockaddr *>(&responder), &responderLength);

                    if (received <= 0) {
                        traceHops[traceHopCount++] = "*  timeout";
                    } else {
                        size_t icmpOffset = 0;
                        if (!resolveIcmpOffset(response, received, icmpOffset)) {
                            traceHops[traceHopCount++] = "Invalid reply";
                        } else {
                            const uint8_t type = response[icmpOffset];
                            const uint8_t *octets = reinterpret_cast<const uint8_t *>(
                                &responder.sin_addr.s_addr);
                            const IPAddress hopAddress(octets[0], octets[1], octets[2], octets[3]);
                            traceHops[traceHopCount++] = hopAddress.toString();
                            reachedDestination = type == 3 ||
                                responder.sin_addr.s_addr == targetAddress.sin_addr.s_addr;
                        }
                    }
                    delay(1);
                }
                closesocket(udpSocket);
                closesocket(icmpSocket);
            }
        }
    }
    currentScreen = Screen::TRACE_RESULTS;
    needsRedraw = true;
}

void setSpeedStage(SpeedTestStage stage) {
    portENTER_CRITICAL(&speedTestMux);
    speedTestStage = stage;
    portEXIT_CRITICAL(&speedTestMux);
}

bool synchronizeClock() {
    if (time(nullptr) >= 1700000000) return true;
    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
    for (uint8_t attempt = 0; attempt < 12; ++attempt) {
        if (time(nullptr) >= 1700000000) return true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

int32_t speedRateCentiMbps(uint32_t bytes, uint32_t elapsedMs) {
    if (elapsedMs == 0) return -1;
    const uint64_t rate = (static_cast<uint64_t>(bytes) * 800) /
        (static_cast<uint64_t>(elapsedMs) * 1000);
    return rate > INT32_MAX ? INT32_MAX : static_cast<int32_t>(rate);
}

bool measureDownload(int32_t &rateCentiMbps) {
    constexpr uint32_t downloadBytes = 1024 * 1024;
    WiFiClientSecure secureClient;
    secureClient.useBuiltinCACertBundle();
    secureClient.setHandshakeTimeout(8);
    secureClient.setTimeout(5000);
    if (!secureClient.connect("speed.cloudflare.com", 443, 8000)) return false;
    secureClient.print("GET /__down?bytes=1048576 HTTP/1.1\r\n");
    secureClient.print("Host: speed.cloudflare.com\r\n");
    secureClient.print("Accept-Encoding: identity\r\nCache-Control: no-cache\r\n");
    secureClient.print("Connection: close\r\n\r\n");

    const String statusLine = secureClient.readStringUntil('\n');
    if (!statusLine.startsWith("HTTP/") || statusLine.indexOf(" 200 ") < 0) {
        secureClient.stop();
        return false;
    }
    while (secureClient.connected()) {
        const String header = secureClient.readStringUntil('\n');
        if (header == "\r" || header.length() == 0) break;
    }

    uint8_t buffer[1024];
    uint32_t received = 0;
    const unsigned long started = millis();
    while (received < downloadBytes && millis() - started < 15000) {
        const int available = secureClient.available();
        if (available > 0) {
            uint32_t amount = available < static_cast<int>(sizeof(buffer))
                ? static_cast<uint32_t>(available) : sizeof(buffer);
            if (amount > downloadBytes - received) amount = downloadBytes - received;
            const int count = secureClient.read(buffer, amount);
            if (count > 0) {
                received += static_cast<uint32_t>(count);
            }
        } else if (!secureClient.connected()) {
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    const uint32_t elapsed = millis() - started;
    secureClient.stop();
    if (received == 0) return false;
    rateCentiMbps = speedRateCentiMbps(received, elapsed);
    return true;
}

bool measureUpload(int32_t &rateCentiMbps) {
    constexpr uint32_t uploadBytes = 256 * 1024;
    WiFiClientSecure secureClient;
    secureClient.useBuiltinCACertBundle();
    secureClient.setHandshakeTimeout(8);
    secureClient.setTimeout(2500);
    if (!secureClient.connect("speed.cloudflare.com", 443, 8000)) return false;

    secureClient.print("POST /__up HTTP/1.1\r\nHost: speed.cloudflare.com\r\n");
    secureClient.print("Origin: https://speed.cloudflare.com\r\n");
    secureClient.print("Content-Type: application/octet-stream\r\n");
    secureClient.print("Content-Length: 262144\r\nConnection: close\r\n\r\n");

    uint8_t buffer[1024];
    for (uint16_t index = 0; index < sizeof(buffer); ++index) {
        buffer[index] = static_cast<uint8_t>((index * 31 + index / 7) & 0xFF);
    }
    uint32_t sent = 0;
    const unsigned long started = millis();
    while (sent < uploadBytes && millis() - started < 15000) {
        const size_t amount = uploadBytes - sent < sizeof(buffer)
            ? uploadBytes - sent : sizeof(buffer);
        const size_t written = secureClient.write(buffer, amount);
        if (written > 0) {
            sent += static_cast<uint32_t>(written);
        } else if (!secureClient.connected()) {
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    const uint32_t elapsed = millis() - started;
    const String response = secureClient.readStringUntil('\n');
    secureClient.stop();
    if (sent == 0 || !response.startsWith("HTTP/") ||
        response.indexOf(" 2") < 0) return false;
    rateCentiMbps = speedRateCentiMbps(sent, elapsed);
    return true;
}

void speedTestWorker(void *parameter) {
    (void)parameter;
    if (!synchronizeClock()) {
        portENTER_CRITICAL(&speedTestMux);
        speedTestState = SpeedTestState::FAILED;
        portEXIT_CRITICAL(&speedTestMux);
        vTaskDelete(nullptr);
        return;
    }

    IPAddress endpoint;
    if (!WiFi.hostByName("speed.cloudflare.com", endpoint)) {
        portENTER_CRITICAL(&speedTestMux);
        speedTestState = SpeedTestState::FAILED;
        portEXIT_CRITICAL(&speedTestMux);
        vTaskDelete(nullptr);
        return;
    }

    setSpeedStage(SpeedTestStage::LATENCY);
    int32_t latency = -1;
    const int pingSocket = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (pingSocket >= 0) {
        timeval timeout = {0, 700000};
        setsockopt(pingSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        uint16_t responseMs = 0;
        if (pingAddress(pingSocket, endpoint,
            static_cast<uint16_t>(ESP.getEfuseMac()), 1, responseMs)) {
            latency = responseMs;
        }
        closesocket(pingSocket);
    }
    portENTER_CRITICAL(&speedTestMux);
    speedLatencyMs = latency;
    portEXIT_CRITICAL(&speedTestMux);

    setSpeedStage(SpeedTestStage::DOWNLOAD);
    int32_t downloadRate = -1;
    const bool downloadOk = measureDownload(downloadRate);
    portENTER_CRITICAL(&speedTestMux);
    speedDownloadCentiMbps = downloadOk ? downloadRate : -1;
    portEXIT_CRITICAL(&speedTestMux);

    setSpeedStage(SpeedTestStage::UPLOAD);
    int32_t uploadRate = -1;
    const bool uploadOk = measureUpload(uploadRate);
    portENTER_CRITICAL(&speedTestMux);
    speedUploadCentiMbps = uploadOk ? uploadRate : -1;
    speedTestState = downloadOk || uploadOk ? SpeedTestState::COMPLETE : SpeedTestState::FAILED;
    portEXIT_CRITICAL(&speedTestMux);
    vTaskDelete(nullptr);
}

void startSpeedTest() {
    currentScreen = Screen::SPEED_TEST;
    if (WiFi.status() != WL_CONNECTED) {
        portENTER_CRITICAL(&speedTestMux);
        speedTestState = SpeedTestState::OFFLINE;
        portEXIT_CRITICAL(&speedTestMux);
        needsRedraw = true;
        return;
    }

    portENTER_CRITICAL(&speedTestMux);
    if (speedTestState == SpeedTestState::RUNNING) {
        portEXIT_CRITICAL(&speedTestMux);
        needsRedraw = true;
        return;
    }
    speedTestState = SpeedTestState::RUNNING;
    speedTestStage = SpeedTestStage::CLOCK;
    speedLatencyMs = -1;
    speedDownloadCentiMbps = -1;
    speedUploadCentiMbps = -1;
    portEXIT_CRITICAL(&speedTestMux);

    needsRedraw = true;
    if (xTaskCreate(speedTestWorker, "speedTest", 6144, nullptr, 1, nullptr) != pdPASS) {
        portENTER_CRITICAL(&speedTestMux);
        speedTestState = SpeedTestState::FAILED;
        portEXIT_CRITICAL(&speedTestMux);
        needsRedraw = true;
    }
}

void beginToolInput(ToolInput mode) {
    inputMode = mode;
    inputValue = "";
    currentScreen = Screen::KEYBOARD;
    needsRedraw = true;
}

void submitToolInput() {
    const String target = inputValue;
    if (target.length() == 0) {
        return;
    }
    if (inputMode == ToolInput::DNS) {
        runDnsCheck(target);
    } else if (inputMode == ToolInput::HTTP) {
        runHttpCheck(target);
    } else if (inputMode == ToolInput::PORT) {
        runPortCheck(target);
    } else if (inputMode == ToolInput::PING) {
        runPingCheck(target);
    } else if (inputMode == ToolInput::TRACE) {
        runTraceCheck(target);
    }
}

void drawCurrentScreen() {
    drawHeader();
    switch (currentScreen) {
        case Screen::HOME:
            drawHome();
            break;
        case Screen::SCAN:
            drawScan();
            break;
        case Screen::DETAILS:
            drawNetworkDetails();
            break;
        case Screen::SETUP:
            drawSetup();
            break;
        case Screen::TOOLS:
            drawTools();
            break;
        case Screen::KEYBOARD:
            drawInput();
            break;
        case Screen::TRACE_RESULTS:
            drawTraceResults();
            break;
        case Screen::LAN_DISCOVERY:
            drawLanDiscovery();
            break;
        case Screen::SPEED_TEST:
            drawSpeedTest();
            break;
        case Screen::PORT_RESULTS:
            drawPortResults();
            break;
    }
    drawFooter();
    needsRedraw = false;
}

void startWifiScan() {
    wifiScrollOffset = 0;
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    currentScreen = Screen::SCAN;
    needsRedraw = true;
}

void scrollWifiResults(int16_t touchY) {
    const int16_t scanResult = WiFi.scanComplete();
    if (scanResult <= static_cast<int16_t>(LAN_VISIBLE_ROWS)) {
        wifiScrollOffset = 0;
        needsRedraw = true;
        return;
    }
    const uint16_t maxOffset = static_cast<uint16_t>(scanResult) - LAN_VISIBLE_ROWS;
    const int16_t trackTop = 66;
    const int16_t trackHeight = 130;
    const int16_t clampedY = constrain(touchY, trackTop, trackTop + trackHeight);
    wifiScrollOffset = static_cast<uint16_t>(
        (clampedY - trackTop) * maxOffset / trackHeight);
    needsRedraw = true;
}

const char *manufacturerForMac(const uint8_t *mac) {
    if ((mac[0] == 0x24 && mac[1] == 0x6F && mac[2] == 0x28) ||
        (mac[0] == 0x24 && mac[1] == 0x0A && mac[2] == 0xC4) ||
        (mac[0] == 0x30 && mac[1] == 0xAE && mac[2] == 0xA4) ||
        (mac[0] == 0x3C && mac[1] == 0x71 && mac[2] == 0xBF) ||
        (mac[0] == 0x7C && mac[1] == 0xDF && mac[2] == 0xA1) ||
        (mac[0] == 0x84 && mac[1] == 0xF3 && mac[2] == 0xEB)) return "Espressif";
    if ((mac[0] == 0x3C && mac[1] == 0x5A && mac[2] == 0xB4) ||
        (mac[0] == 0xF4 && mac[1] == 0xF5 && mac[2] == 0xD8)) return "Google";
    if ((mac[0] == 0xB8 && mac[1] == 0x27 && mac[2] == 0xEB) ||
        (mac[0] == 0xDC && mac[1] == 0xA6 && mac[2] == 0x32) ||
        (mac[0] == 0xE4 && mac[1] == 0x5F && mac[2] == 0x01)) return "Raspberry Pi";
    if ((mac[0] == 0x00 && mac[1] == 0x16 && mac[2] == 0x32) ||
        (mac[0] == 0x00 && mac[1] == 0x1D && mac[2] == 0x25) ||
        (mac[0] == 0x8C && mac[1] == 0x77 && mac[2] == 0x12)) return "Samsung";
    if ((mac[0] == 0x00 && mac[1] == 0x1B && mac[2] == 0x63) ||
        (mac[0] == 0x00 && mac[1] == 0x1C && mac[2] == 0xB3) ||
        (mac[0] == 0x3C && mac[1] == 0x22 && mac[2] == 0xFB)) return "Apple";
    return nullptr;
}

int16_t findLanDevice(const IPAddress &address) {
    const uint32_t target = ipToInteger(address);
    int16_t found = -1;
    portENTER_CRITICAL(&lanScanMux);
    for (uint16_t index = 0; index < lanDeviceCount; ++index) {
        if (ipToInteger(lanDevices[index].address) == target) {
            found = static_cast<int16_t>(index);
            break;
        }
    }
    portEXIT_CRITICAL(&lanScanMux);
    return found;
}

void storeLanMetadata(int16_t index, const String &hostname, const String &manufacturer) {
    if (index < 0) return;
    portENTER_CRITICAL(&lanScanMux);
    LanDevice &device = lanDevices[index];
    if (hostname.length() > 0 && device.hostname[0] == '\0') {
        strncpy(device.hostname, hostname.c_str(), sizeof(device.hostname) - 1);
        device.hostname[sizeof(device.hostname) - 1] = '\0';
    }
    if (manufacturer.length() > 0) {
        strncpy(device.manufacturer, manufacturer.c_str(), sizeof(device.manufacturer) - 1);
        device.manufacturer[sizeof(device.manufacturer) - 1] = '\0';
    }
    portEXIT_CRITICAL(&lanScanMux);
}

bool parseMacAddress(const String &value, uint8_t mac[6]) {
    uint8_t bytesRead = 0;
    int highNibble = -1;
    for (size_t index = 0; index < value.length(); ++index) {
        const char character = value[index];
        int nibble = -1;
        if (character >= '0' && character <= '9') nibble = character - '0';
        else if (character >= 'a' && character <= 'f') nibble = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') nibble = character - 'A' + 10;
        if (nibble < 0) continue;
        if (highNibble < 0) {
            highNibble = nibble;
        } else {
            if (bytesRead >= 6) return false;
            mac[bytesRead++] = static_cast<uint8_t>((highNibble << 4) | nibble);
            highNibble = -1;
        }
    }
    return bytesRead == 6 && highNibble < 0;
}

String xmlValue(const String &xml, const char *tag) {
    String lowerXml = xml;
    lowerXml.toLowerCase();
    String lowerTag = tag;
    lowerTag.toLowerCase();
    const String openTag = String("<") + lowerTag + ">";
    const String closeTag = String("</") + lowerTag + ">";
    const int start = lowerXml.indexOf(openTag);
    if (start < 0) return "";
    const int valueStart = start + openTag.length();
    const int end = lowerXml.indexOf(closeTag, valueStart);
    if (end < 0) return "";
    String value = xml.substring(valueStart, end);
    value.trim();
    value.replace("&amp;", "&");
    value.replace("&lt;", "<");
    value.replace("&gt;", ">");
    return value;
}

String httpHeaderValue(const String &headers, const char *key) {
    String lowerHeaders = headers;
    lowerHeaders.toLowerCase();
    String lowerKey = key;
    lowerKey.toLowerCase();
    const int headerStart = lowerHeaders.indexOf(lowerKey + ":");
    if (headerStart < 0) return "";
    const int valueStart = headerStart + lowerKey.length() + 1;
    int valueEnd = headers.indexOf("\r\n", valueStart);
    if (valueEnd < 0) valueEnd = headers.length();
    String value = headers.substring(valueStart, valueEnd);
    value.trim();
    return value;
}

String fetchSsdpDescription(const IPAddress &address, const String &location) {
    String lowerLocation = location;
    lowerLocation.toLowerCase();
    if (!lowerLocation.startsWith("http://")) return "";
    const String authorityAndPath = location.substring(7);
    int slash = authorityAndPath.indexOf('/');
    String authority = slash < 0 ? authorityAndPath : authorityAndPath.substring(0, slash);
    String path = slash < 0 ? "/" : authorityAndPath.substring(slash);
    uint16_t port = 80;
    const int colon = authority.lastIndexOf(':');
    if (colon >= 0) {
        port = static_cast<uint16_t>(authority.substring(colon + 1).toInt());
        authority = authority.substring(0, colon);
    }
    if (port == 0) return "";

    WiFiClient client;
    client.setTimeout(400);
    if (!client.connect(address, port, 400)) return "";
    client.print("GET ");
    client.print(path);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(authority);
    client.print("\r\nConnection: close\r\n\r\n");

    String response;
    const unsigned long started = millis();
    while (millis() - started < 900 && response.length() < 2048) {
        while (client.available() && response.length() < 2048) {
            response += static_cast<char>(client.read());
        }
        if (!client.connected() && !client.available()) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    client.stop();
    const int bodyStart = response.indexOf("\r\n\r\n");
    return bodyStart < 0 ? "" : response.substring(bodyStart + 4);
}

void enrichWithMdns() {
    if (!mdnsStarted) mdnsStarted = MDNS.begin("cerbsys-nettools");
    if (!mdnsStarted) return;
    static const char *services[] = {"http", "ipp", "googlecast"};
    for (const char *service : services) {
        const int count = MDNS.queryService(service, "tcp");
        for (int result = 0; result < count; ++result) {
            const IPAddress address = MDNS.address(result);
            const int16_t deviceIndex = findLanDevice(address);
            if (deviceIndex < 0) continue;
            String hostname = MDNS.hostname(result);
            if (hostname.length() == 0) hostname = MDNS.instanceName(result);

            String manufacturer;
            for (int txtIndex = 0; txtIndex < MDNS.numTxt(result); ++txtIndex) {
                String key = MDNS.txtKey(result, txtIndex);
                key.toLowerCase();
                if (key == "mac" || key == "macaddress") {
                    uint8_t mac[6];
                    if (parseMacAddress(MDNS.txt(result, txtIndex), mac)) {
                        const char *name = manufacturerForMac(mac);
                        if (name != nullptr) manufacturer = name;
                    }
                }
            }
            storeLanMetadata(deviceIndex, hostname, manufacturer);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void enrichWithSsdp() {
    WiFiUDP udp;
    if (!udp.begin(1901)) return;
    const IPAddress multicast(239, 255, 255, 250);
    static const char request[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: ssdp:all\r\n\r\n";
    if (udp.beginPacket(multicast, 1900) == 0) {
        udp.stop();
        return;
    }
    udp.write(reinterpret_cast<const uint8_t *>(request), sizeof(request) - 1);
    udp.endPacket();

    const unsigned long started = millis();
    uint8_t descriptionsFetched = 0;
    while (millis() - started < 1800 && descriptionsFetched < 8) {
        const int packetSize = udp.parsePacket();
        if (packetSize > 0) {
            char packet[1200];
            const int bytesToRead = packetSize < static_cast<int>(sizeof(packet) - 1)
                ? packetSize : sizeof(packet) - 1;
            const int bytesRead = udp.read(packet, bytesToRead);
            if (bytesRead > 0) {
                packet[bytesRead] = '\0';
                const IPAddress address = udp.remoteIP();
                const int16_t deviceIndex = findLanDevice(address);
                if (deviceIndex >= 0) {
                    const String location = httpHeaderValue(String(packet), "location");
                    if (location.length() > 0) {
                        const String xml = fetchSsdpDescription(address, location);
                        String hostname = xmlValue(xml, "friendlyName");
                        String manufacturer = xmlValue(xml, "manufacturer");
                        String macText = xmlValue(xml, "macAddress");
                        if (macText.length() == 0) macText = xmlValue(xml, "mac");
                        if (manufacturer.length() == 0 && macText.length() > 0) {
                            uint8_t mac[6];
                            if (parseMacAddress(macText, mac)) {
                                const char *name = manufacturerForMac(mac);
                                if (name != nullptr) manufacturer = name;
                            }
                        }
                        storeLanMetadata(deviceIndex, hostname, manufacturer);
                        ++descriptionsFetched;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    udp.stop();
}

void lanDiscoveryWorker(void *parameter) {
    (void)parameter;
    const int socketFd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socketFd < 0) {
        portENTER_CRITICAL(&lanScanMux);
        lanScanState = LAN_SCAN_ICMP_UNAVAILABLE;
        portEXIT_CRITICAL(&lanScanMux);
        vTaskDelete(nullptr);
        return;
    }

    timeval timeout = {0, 45000};
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const uint16_t identifier = static_cast<uint16_t>(ESP.getEfuseMac());
    uint16_t sequence = 0;
    for (uint16_t offset = 1; offset <= lanHostCount; ++offset) {
        const uint32_t targetValue = lanNetworkStart + offset;
        if (targetValue != lanLocalAddress) {
            uint16_t responseMs = 0;
            ++sequence;
            const IPAddress target = integerToIp(targetValue);
            if (pingAddress(socketFd, target, identifier, sequence, responseMs)) {
                portENTER_CRITICAL(&lanScanMux);
                if (lanDeviceCount < LAN_MAX_HOSTS) {
                    lanDevices[lanDeviceCount].address = target;
                    lanDevices[lanDeviceCount].responseMs = responseMs;
                    lanDevices[lanDeviceCount].hostname[0] = '\0';
                    lanDevices[lanDeviceCount].manufacturer[0] = '\0';
                    lanDeviceCount = lanDeviceCount + 1;
                }
                portEXIT_CRITICAL(&lanScanMux);
            }
            portENTER_CRITICAL(&lanScanMux);
            lanScanProgress = lanScanProgress + 1;
            portEXIT_CRITICAL(&lanScanMux);
            vTaskDelay(pdMS_TO_TICKS(8));
        }
    }

    closesocket(socketFd);
    portENTER_CRITICAL(&lanScanMux);
    lanScanState = LAN_SCAN_ENRICHING;
    portEXIT_CRITICAL(&lanScanMux);
    enrichWithMdns();
    enrichWithSsdp();
    portENTER_CRITICAL(&lanScanMux);
    lanScanState = LAN_SCAN_COMPLETE;
    portEXIT_CRITICAL(&lanScanMux);
    vTaskDelete(nullptr);
}

void runLanDiscovery() {
    if (lanScanState == LAN_SCAN_RUNNING) {
        currentScreen = Screen::LAN_DISCOVERY;
        needsRedraw = true;
        return;
    }

    currentScreen = Screen::LAN_DISCOVERY;
    lanScrollOffset = 0;
    const IPAddress localAddress = WiFi.localIP();
    if (WiFi.status() != WL_CONNECTED || !isPrivateAddress(localAddress)) {
        portENTER_CRITICAL(&lanScanMux);
        lanScanState = LAN_SCAN_OFFLINE;
        lanDeviceCount = 0;
        portEXIT_CRITICAL(&lanScanMux);
        needsRedraw = true;
        return;
    }

    const uint32_t addressValue = ipToInteger(localAddress);
    const uint32_t maskValue = ipToInteger(WiFi.subnetMask());
    const uint32_t networkValue = addressValue & maskValue;
    const uint32_t broadcastValue = networkValue | ~maskValue;
    if (broadcastValue <= networkValue + 1) {
        portENTER_CRITICAL(&lanScanMux);
        lanScanState = LAN_SCAN_TOO_LARGE;
        lanDeviceCount = 0;
        portEXIT_CRITICAL(&lanScanMux);
        needsRedraw = true;
        return;
    }

    const uint32_t hostCount = broadcastValue - networkValue - 1;
    if (hostCount > LAN_MAX_HOSTS) {
        portENTER_CRITICAL(&lanScanMux);
        lanScanState = LAN_SCAN_TOO_LARGE;
        lanDeviceCount = 0;
        portEXIT_CRITICAL(&lanScanMux);
        needsRedraw = true;
        return;
    }

    lanNetworkStart = networkValue;
    lanLocalAddress = addressValue;
    lanHostCount = static_cast<uint16_t>(hostCount);
    portENTER_CRITICAL(&lanScanMux);
    lanDeviceCount = 0;
    lanScanProgress = 0;
    lanScanTotal = static_cast<uint16_t>(hostCount - 1);
    lanScanState = LAN_SCAN_RUNNING;
    portEXIT_CRITICAL(&lanScanMux);
    needsRedraw = true;
    drawCurrentScreen();

    if (xTaskCreate(lanDiscoveryWorker, "lanDiscovery", 4096, nullptr, 1, nullptr) != pdPASS) {
        portENTER_CRITICAL(&lanScanMux);
        lanScanState = LAN_SCAN_TASK_FAILED;
        portEXIT_CRITICAL(&lanScanMux);
        needsRedraw = true;
    }
}

void scrollLanResults(int16_t touchY) {
    uint16_t deviceCount;
    portENTER_CRITICAL(&lanScanMux);
    deviceCount = lanDeviceCount;
    portEXIT_CRITICAL(&lanScanMux);
    const uint16_t maxOffset = deviceCount > LAN_VISIBLE_ROWS
        ? deviceCount - LAN_VISIBLE_ROWS : 0;
    const int16_t trackTop = 77;
    const int16_t trackHeight = 120;
    const int16_t clampedY = constrain(touchY, trackTop, trackTop + trackHeight);
    lanScrollOffset = maxOffset == 0 ? 0 :
        static_cast<uint16_t>((clampedY - trackTop) * maxOffset / trackHeight);
    needsRedraw = true;
}

void handleInputTap(int16_t x, int16_t y) {
    if (y >= 191 && y <= 212) {
        if (x < 100) {
            currentScreen = Screen::TOOLS;
            needsRedraw = true;
        } else if (x < 210) {
            if (inputValue.length() > 0) {
                inputValue.remove(inputValue.length() - 1);
                needsRedraw = true;
            }
        } else {
            submitToolInput();
        }
        return;
    }

    static const char *rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm.-"};
    static const int16_t rowY[] = {94, 118, 142, 166};
    for (uint8_t row = 0; row < 4; ++row) {
        if (y < rowY[row] || y >= rowY[row] + 21) {
            continue;
        }
        const int16_t length = strlen(rows[row]);
        const int16_t startX = (DISPLAY_WIDTH - length * 29 + 3) / 2;
        const int16_t column = (x - startX) / 29;
        if (x >= startX && column >= 0 && column < length && inputValue.length() < 63) {
            inputValue += rows[row][column];
            needsRedraw = true;
        }
        return;
    }
}

void handleTap(int16_t x, int16_t y) {
    if (currentScreen == Screen::KEYBOARD) {
        handleInputTap(x, y);
        return;
    }

    if (y >= 214) {
        if (x < 106) {
            stopSetupPortal();
            currentScreen = Screen::HOME;
        } else if (x < 212) {
            currentScreen = Screen::TOOLS;
        } else {
            currentScreen = Screen::DETAILS;
        }
        needsRedraw = true;
        return;
    }

    if (currentScreen == Screen::HOME && y >= 145 && y <= 200) {
        if (x < 108) {
            startSetupPortal();
        } else if (x < 212) {
            currentScreen = Screen::DETAILS;
            needsRedraw = true;
        } else {
            currentScreen = Screen::TOOLS;
            needsRedraw = true;
        }
    } else if (currentScreen == Screen::SCAN && x >= 250 && y < 70) {
        startWifiScan();
    } else if (currentScreen == Screen::DETAILS && y >= 177 && y <= 207 && x >= 188) {
        if (WiFi.status() == WL_CONNECTED) {
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false, false);
        } else if (hasWifiCredentials()) {
            WiFi.setAutoReconnect(true);
            WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
        } else {
            startSetupPortal();
        }
        needsRedraw = true;
    } else if (currentScreen == Screen::TOOLS && y >= 60 && y <= 88) {
        if (x < 160) {
            beginToolInput(ToolInput::DNS);
        } else {
            beginToolInput(ToolInput::HTTP);
        }
    } else if (currentScreen == Screen::TOOLS && y >= 93 && y <= 121) {
        if (x < 160) {
            startWifiScan();
        } else {
            beginToolInput(ToolInput::PORT);
        }
    } else if (currentScreen == Screen::TOOLS && y >= 126 && y <= 154) {
        if (x < 160) {
            beginToolInput(ToolInput::PING);
        } else {
            beginToolInput(ToolInput::TRACE);
        }
    } else if (currentScreen == Screen::TOOLS && y >= 159 && y <= 187) {
        if (x < 160) {
            runLanDiscovery();
        } else {
            startSpeedTest();
        }
    } else if (currentScreen == Screen::SPEED_TEST && y >= 180 && y <= 209 && x >= 211) {
        startSpeedTest();
    }
}

void setup() {
    Serial.begin(115200);
    initializeDisplay();
    initializeTouch();

    preferences.begin("nettools", true);
    wifiSsid = preferences.getString("ssid", "");
    wifiPassword = preferences.getString("password", "");
    preferences.end();
    if (wifiSsid.length() == 0 && WIFI_SSID[0] != '\0') {
        wifiSsid = WIFI_SSID;
        wifiPassword = WIFI_PASSWORD;
    }

    WiFi.mode(WIFI_STA);
    if (hasWifiCredentials()) {
        WiFi.setAutoReconnect(true);
        WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }

    drawCurrentScreen();
}

void loop() {
    if (setupPortalActive) {
        server.handleClient();
    }

    if (touch.touched()) {
        TS_Point point = touch.getPoint();
        const int16_t x = constrain(map(point.x, 200, 3800, 0, DISPLAY_WIDTH), 0, DISPLAY_WIDTH - 1);
        const int16_t y = constrain(map(point.y, 200, 3800, 0, DISPLAY_HEIGHT), 0, DISPLAY_HEIGHT - 1);
        if (currentScreen == Screen::LAN_DISCOVERY && x >= 296 && y >= 77 && y <= 197) {
            scrollLanResults(y);
            touchLatched = true;
        } else if (currentScreen == Screen::SCAN && x >= 296 && y >= 66 && y <= 196) {
            scrollWifiResults(y);
            touchLatched = true;
        } else if (!touchLatched) {
            handleTap(x, y);
            touchLatched = true;
        }
    } else {
        touchLatched = false;
    }

    if (millis() - lastRefresh >= 1000) {
        lastRefresh = millis();
        if (currentScreen == Screen::HOME || currentScreen == Screen::DETAILS ||
            currentScreen == Screen::SETUP ||
            (currentScreen == Screen::SCAN && WiFi.scanComplete() != WIFI_SCAN_RUNNING) ||
            currentScreen == Screen::LAN_DISCOVERY || currentScreen == Screen::SPEED_TEST) {
            needsRedraw = true;
        }
    }

    if (needsRedraw) {
        drawCurrentScreen();
    }
    delay(20);
}
