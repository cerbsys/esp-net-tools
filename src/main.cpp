
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>

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
    TRACE_RESULTS
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
unsigned long lastRefresh = 0;
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
            currentScreen == Screen::TRACE_RESULTS ? Colors::ACCENT : Colors::GRAY);
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
        const int16_t rows = scanResult < 5 ? scanResult : 5;
        for (int16_t index = 0; index < rows; ++index) {
            const int16_t y = 66 + index * 29;
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
        const int16_t y = 68 + index * 27;
        drawText(labels[index], 14, y, Colors::GRAY);
        drawText(values[index], 92, y, Colors::WHITE);
        tft.drawFastHLine(12, y + 16, 296, Colors::PANEL);
    }
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

    const int16_t buttonY[] = {62, 100, 138};
    for (uint8_t row = 0; row < 3; ++row) {
        tft.drawRoundRect(8, buttonY[row], 145, 32, 4, Colors::ACCENT);
        tft.drawRoundRect(165, buttonY[row], 145, 32, 4, Colors::ACCENT);
    }
    drawText("DNS LOOKUP", 44, 73, Colors::ACCENT);
    drawText("HTTP CHECK", 199, 73, Colors::ACCENT);
    drawText("WI-FI SCAN", 43, 111, Colors::ACCENT);
    drawText("PORTS", 222, 111, Colors::ACCENT);
    drawText("PING", 67, 149, Colors::ACCENT);
    drawText("TRACE", 221, 149, Colors::ACCENT);
    drawText("PING", 67, 149, Colors::ACCENT);
    drawText("TRACE", 221, 149, Colors::ACCENT);
    drawText(toolTitle.substring(0, 34), 12, 178, Colors::WHITE);
    drawText(toolResult.substring(0, 34), 12, 195, Colors::GREEN);
    if (WiFi.status() != WL_CONNECTED) {
        drawText("OFFLINE", 246, 195, Colors::YELLOW);
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

void runPortCheck(const String &target) {
    toolTitle = "Ports: " + target;
    IPAddress address;
    if (!address.fromString(target) || !isPrivateAddress(address)) {
        toolResult = "Use a private LAN IPv4 address";
    } else if (WiFi.status() != WL_CONNECTED) {
        toolResult = "Not connected";
    } else {
        static const uint16_t ports[] = {22, 53, 80, 443, 445, 8080};
        String openPorts = "Open:";
        for (uint8_t index = 0; index < sizeof(ports) / sizeof(ports[0]); ++index) {
            WiFiClient client;
            if (client.connect(address, ports[index], 300)) {
                openPorts += " " + String(ports[index]);
                client.stop();
            }
        }
        toolResult = openPorts == "Open:" ? "No common ports open" : openPorts;
    }
    currentScreen = Screen::TOOLS;
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
    }
    drawFooter();
    needsRedraw = false;
}

void startWifiScan() {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    currentScreen = Screen::SCAN;
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
    } else if (currentScreen == Screen::TOOLS && y >= 62 && y <= 94) {
        if (x < 160) {
            beginToolInput(ToolInput::DNS);
        } else {
            beginToolInput(ToolInput::HTTP);
        }
    } else if (currentScreen == Screen::TOOLS && y >= 100 && y <= 132) {
        if (x < 160) {
            startWifiScan();
        } else {
            beginToolInput(ToolInput::PORT);
        }
    } else if (currentScreen == Screen::TOOLS && y >= 138 && y <= 170) {
        if (x < 160) {
            beginToolInput(ToolInput::PING);
        } else {
            beginToolInput(ToolInput::TRACE);
        }
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
        if (!touchLatched) {
            TS_Point point = touch.getPoint();
            const int16_t x = constrain(map(point.x, 200, 3800, 0, DISPLAY_WIDTH), 0, DISPLAY_WIDTH - 1);
            const int16_t y = constrain(map(point.y, 200, 3800, 0, DISPLAY_HEIGHT), 0, DISPLAY_HEIGHT - 1);
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
            (currentScreen == Screen::SCAN && WiFi.scanComplete() != WIFI_SCAN_RUNNING)) {
            needsRedraw = true;
        }
    }

    if (needsRedraw) {
        drawCurrentScreen();
    }
    delay(20);
}
