#include <SPI.h>
#include <Ethernet.h>

// ── Pin definitions ────────────────────────────────────────────────────────
#define ETH_CLK    13
#define ETH_CS     14
#define ETH_MISO   12
#define ETH_MOSI   11
#define ETH_RESET   9
#define ETH_INT    10    // Wired but not used in this project
#define RELAY_PIN  45

// ── Network configuration ──────────────────────────────────────────────────
// MAC address — must be unique on your LAN.
// The label on your board, or keep this default for a single-board setup.
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Static IP — change to suit your network (or use DHCP, see Experiments).
IPAddress ip(192, 168, 1, 177);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// ── Globals ────────────────────────────────────────────────────────────────
EthernetServer server(80);   // Listen on port 80 (standard HTTP)
bool relay_sts = false;      // Relay starts de-energised

// ── HTML page ──────────────────────────────────────────────────────────────
// Stored in flash (F macro) to save RAM.
// The %STATE% and %BTN% tokens are replaced at runtime.
const char HTML_TEMPLATE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Relay Control</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:sans-serif;background:#0f1923;color:#e0e6ed;display:grid;place-items:center;min-height:100vh}
    .card{background:#1a2535;border:1px solid #2a3a50;border-radius:16px;padding:2.5rem;text-align:center;width:320px;box-shadow:0 8px 32px rgba(0,0,0,.4)}
    h1{font-size:1.25rem;letter-spacing:.08em;color:#7a9bbe;margin-bottom:2rem;text-transform:uppercase}
    .indicator{width:80px;height:80px;border-radius:50%;margin:0 auto 1.5rem;display:flex;align-items:center;justify-content:center;font-size:.75rem;font-weight:700;transition:.3s}
    .on{background:#1db954;box-shadow:0 0 24px #1db95488;color:#fff}
    .off{background:#2a3a50;color:#7a9bbe}
    .status-label{font-size:.9rem;color:#7a9bbe;margin-bottom:2rem}
    .status-label span{font-weight:700;color:#e0e6ed}
    button{width:100%;padding:.85rem;border:none;border-radius:8px;font-weight:700;cursor:pointer}
    button:active{transform:scale(.97)}
    .btn-on{background:#1db954;color:#fff}
    .btn-off{background:#c0392b;color:#fff}
    .footer{margin-top:2rem;font-size:.72rem;color:#3a5a7a}
  </style>
</head>
<body>
  <div class="card">
    <h1>Relay Control</h1>
    <div class="indicator %INDCLASS%">%INDLABEL%</div>
    <p class="status-label">Relay is <span>%STATE%</span></p>
    <form method="POST" action="/toggle">
      <button class="%BTNCLASS%">%BTNLABEL%</button>
    </form>
    <p class="footer">STEAM Dev Board &bull; Ethernet LAN</p>
  </div>
</body>
</html>
)rawhtml";

// ── Helper: send the control page ─────────────────────────────────────────
void sendPage(EthernetClient& client) {
    // Build the response string from the template, substituting live values.
    String page = String((__FlashStringHelper*)HTML_TEMPLATE);
    if (relay_sts) {
        page.replace("%STATE%",    "ON");
        page.replace("%INDCLASS%", "on");
        page.replace("%INDLABEL%", "ON");
        page.replace("%BTNCLASS%", "btn-off");
        page.replace("%BTNLABEL%", "Turn OFF");
    } else {
        page.replace("%STATE%",    "OFF");
        page.replace("%INDCLASS%", "off");
        page.replace("%INDLABEL%", "OFF");
        page.replace("%BTNCLASS%", "btn-on");
        page.replace("%BTNLABEL%", "Turn ON");
    }

    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/html"));
    client.println(F("Connection: close"));
    client.print(F("Content-Length: "));
    client.println(page.length());
    client.println();
    client.print(page);
}

// ── Helper: send a redirect response ──────────────────────────────────────
void sendRedirect(EthernetClient& client, const char* location) {
    client.println(F("HTTP/1.1 303 See Other"));
    client.print(F("Location: "));
    client.println(location);
    client.println(F("Connection: close"));
    client.println();
}

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Relay
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, relay_sts);

    // Hardware-reset the W5500
    pinMode(ETH_RESET, OUTPUT);
    digitalWrite(ETH_RESET, LOW);
    delay(10);
    digitalWrite(ETH_RESET, HIGH);
    delay(200);   // Allow W5500 to complete its internal reset

    // Remap SPI to the custom pins and init Ethernet
    SPI.begin(ETH_CLK, ETH_MISO, ETH_MOSI, ETH_CS);
    Ethernet.init(ETH_CS);
    Ethernet.begin(mac, ip, gateway, gateway, subnet);

    // Check hardware link
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println(F("ERROR: W5500 not found. Check SPI wiring."));
        while (true) { delay(1000); }  // Halt — nothing can be done
    }
    while (Ethernet.linkStatus() == LinkOFF) {
        Serial.print(F("WARNING: Ethernet cable not connected."));
        Serial.println(Ethernet.linkStatus());
    delay(1000);
    }

    server.begin();

    Serial.print(F("Server ready at http://"));
    Serial.println(Ethernet.localIP());
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
    EthernetClient client = server.available();
    if (!client) return;

    Serial.println(F("Client connected"));

    String request = "";
    bool   firstLine = true;
    String requestLine = "";

    // Read the HTTP request (first line contains method + path)
    while (client.connected()) {
        if (!client.available()) continue;

        char c = client.read();

        if (firstLine) {
            if (c == '\n') {
                firstLine = false;
            } else if (c != '\r') {
                requestLine += c;
            }
        } else {
            // Read and discard the remaining headers
            // until we reach the blank line (\r\n\r\n)
            request += c;
            if (request.endsWith("\r\n\r\n")) break;
        }
    }

    Serial.println("Request: " + requestLine);

    // Route the request
    if (requestLine.startsWith("POST /toggle")) {
        relay_sts = !relay_sts;
        digitalWrite(RELAY_PIN, relay_sts);
        Serial.print(F("Relay → "));
        Serial.println(relay_sts ? F("ON") : F("OFF"));
        sendRedirect(client, "/");          // PRG pattern: redirect after POST
    } else {
        sendPage(client);                   // GET / → serve the control page
    }

    // Give the browser time to receive the response, then close
    delay(10);
    client.stop();
    Serial.println(F("Client disconnected"));
}