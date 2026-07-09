*************************************************************
Project 8 : Wi-Fi Controlled Switch via Web Browser
*************************************************************

Introduction
============ 

In this project, we step into the world of the Internet of Things (IoT). We will leverage the ESP32's built-in Wi-Fi radio to transform our microcontroller into a standalone local **Web Server**. 

Instead of using physical buttons, we will design a control panel web page hosted directly inside the chip's memory. Anyone connected to the network can load this page on a smartphone or PC and toggle an integrated switch over the air.

By the end of this tutorial, you will know how to:

* Configure the ESP32 as a Wi-Fi Access Point (AP).
* Launch a lightweight HTTP web server on Port 80.
* Process incoming HTTP GET requests sent by a web browser.
* Serve custom HTML and CSS styled control buttons wirelessly.

Requirements
============

Before starting, make sure you have the following:

* The STEAM development kit or The STEAM standalone microcontroller board 
* USB cable
* Arduino IDE with ESP32 toolchain installed
* A smartphone, tablet, or laptop with Wi-Fi capability

In this project, the built-in LED on **GPIO08** will act as our relay/switch indicator. No external wiring is required.

.. figure:: ../../img/MCU_board.png
   :align: center
   :width: 400
   :figclass: align-center

   The interfacing description of the microcontroller board 

.. note::
    Because we are setting the ESP32 to **Access Point Mode**, it will broadcast its own custom network name (SSID). You do not need an active internet connection or a home router for this project to work.

Writing the Program
===================

Create a new sketch in the Arduino IDE and enter the following code:

.. code-block:: cpp

    #include <WiFi.h>

    // Define the pin acting as our digital switch (the built-in LED)
    #define RELAY_PIN 8 

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


Uploading the Program
^^^^^^^^^^^^^^^^^^^^^

1. Connect the board to your computer using the USB cable.
2. Select **ESP32S3 Dev Module** and the active serial port from **Tools**.
3. Open the **Serial Monitor** (**Tools → Serial Monitor**) and set the baud rate speed to **115200**.
4. Click the **Upload** button.

Expected Result
^^^^^^^^^^^^^^^

After code completion:

* Look at your Serial Monitor. You will see text confirming ``AP IP address: 192.168.4.1``.
* Grab your smartphone or computer, open your Wi-Fi settings panel, and scan for networks.
* Connect to the network named **STEAM_Web_Switch** using the security password **12345678**.
* Open your device's web browser (Safari, Chrome, Firefox) and type ``192.168.4.1`` directly into the URL address bar and press Enter.
* A control panel page will load. Tap the green **TURN ON** or red **TURN OFF** buttons; the integrated board LED will cleanly change states wireless in real time.

How the Code Works
^^^^^^^^^^^^^^^^^^

**Soft Access Point (softAP)**

Instead of latching onto an external household internet hub, the statement:

.. code-block:: cpp

    WiFi.softAP(ssid, password);

tells the ESP32 firmware to configure itself as a hot spot root router. By default, it generates a gateway IP address of exactly ``192.168.4.1``.

**Parsing Network Strings**

When you click an HTML hyperlink button, the web browser submits an underlying request text payload string to our port server that looks something like this:

.. code-block:: text

    GET /ON HTTP/1.1
    Host: 192.168.4.1
    ...

The script continuously looks for these signatures inside the `loop()` using:

.. code-block:: cpp

    if (currentLine.endsWith("GET /ON")) { ... }

When it catches that specific matching marker text sequence, it reroutes standard system code instructions natively to trip the `digitalWrite()` state high or low.

Experiment
^^^^^^^^^^

* **Customize Security Credentials:** Change the network `ssid` string name and password variable blocks at the top of the sketch file. Test re-authenticating your phone using your unique customized credentials.
* **Audio Visual Hybrid Alert:** Integrate Project 2 or 3 into this sketch. Make it so that when a user taps the web browser interface "ON" button, the board not only trips the relay light indicator but also plays a brief chime verification confirmation tone using the integrated board buzzer.

Summary
=======
In this tutorial, you bridged the boundary line between offline hardware devices and local network data streams. You transformed an embedded board microcontroller chip engine directly into a running local text web server node platform.

Key concepts covered include:

* Distinguishing between Client Station mode and local hosted Access Point options.
* Running an active application listening server port handler using ``WiFiServer``.
* Streaming raw standard hypertext compiled HTML language structures and custom embedded inline layout CSS style blocks natively using standard microcontroller print buffers.
* Decoding browser HTTP link command modifications dynamically to control physical digital hardware structures.