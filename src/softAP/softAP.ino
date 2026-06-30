#include <WiFi.h>

// Define the pin acting as our digital switch (the built-in LED)
#define RELAY_PIN 45

// Set up your private Wi-Fi Network credentials
const char* ssid     = "STEAM_Web_Switch";
const char* password = "12345678"; // Must be at least 8 characters

// Instantiate the server on standard HTTP port 80
WiFiServer server(80);

void setup() {
    // Initialize serial communication for debugging output
    Serial.begin(115200);

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // Start with switch OFF

    Serial.println("\nConfiguring Access Point...");

    // Start broadcasting the Wi-Fi network
    WiFi.softAP(ssid, password);

    // Retrieve and print the server IP address to the Serial Monitor
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);

    // Start the web server
    server.begin();
    Serial.println("Server started successfully!");
}

void loop() {
    // Check if a web browser (client) has connected to our server
    WiFiClient client = server.available();

    if (client) {
        Serial.println("New client connected.");
        String currentLine = ""; // String buffer to hold incoming HTTP request data

        while (client.connected()) {
            if (client.available()) {
                char c = client.read(); // Read bytes sent by the browser
                Serial.write(c);

                if (c == '\n') {
                    // An empty line indicates the end of the HTTP request header
                    if (currentLine.length() == 0) {
                        // Send standard HTTP response headers
                        client.println("HTTP/1.1 200 OK");
                        client.println("Content-type:text/html");
                        client.println("Connection: close");
                        client.println(); // Blank line mandatory separating headers and body

                        // Construct and transmit the HTML/CSS web page code
                        client.print("<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                        client.print("<style>body{font-family:Arial; text-align:center; margin-top:50px;}");
                        client.print(".btn{display:inline-block; padding:20px 40px; font-size:24px; text-decoration:none; color:#fff; border-radius:8px; margin:10px; font-weight:bold;}");
                        client.print(".on{background-color:#2ecc71;} .off{background-color:#e74c3c;}</style></head>");

                        client.print("<body><h1>STEAM Wi-Fi Relay Control</h1>");

                        // Dynamically update status label text on screen
                        if (digitalRead(RELAY_PIN) == HIGH) {
                            client.print("<p>Current State: <span style='color:#2ecc71; font-weight:bold;'>ON</span></p>");
                        } else {
                            client.print("<p>Current State: <span style='color:#e74c3c; font-weight:bold;'>OFF</span></p>");
                        }

                        // Hyperlinked button paths that trigger HTTP GET requests
                        client.print("<a href=\"/ON\"><div class='btn on'>TURN ON</div></a>");
                        client.print("<a href=\"/OFF\"><div class='btn off'>TURN OFF</div></a>");
                        client.print("</body></html>");

                        break; // Exit the loop to finalize sending data
                    } else {
                        currentLine = ""; // Clear buffer on a regular newline
                    }
                } else if (c != '\r') {
                    currentLine += c; // Append incoming characters to our parsing string
                }

                // Route commands based on the browser URL request ending
                if (currentLine.endsWith("GET /ON")) {
                    digitalWrite(RELAY_PIN, HIGH);
                }
                if (currentLine.endsWith("GET /OFF")) {
                    digitalWrite(RELAY_PIN, LOW);
                }
            }
        }
        // Disconnect client connection cleanly to free up resources
        client.stop();
        Serial.println("Client disconnected.");
    }
}