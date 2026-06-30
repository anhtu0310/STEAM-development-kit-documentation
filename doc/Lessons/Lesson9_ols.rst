*************************************************************
Project 9 : Ethernet Controlled Switch via Web Browser
*************************************************************

Introduction
============ 

While Wi-Fi is incredibly convenient, many industrial and smart-home applications require the rock-solid reliability of a hardwired connection. In this project, we will use the **W5500 Ethernet controller** chip to connect our ESP32-S3 directly to a local network via an Ethernet cable.

The W5500 is a fantastic companion chip because it has a "hardwired TCP/IP stack." This means the chip itself handles the complex math of internet protocols, freeing up the ESP32's memory and processing power. We will recreate our Web Server switch from the previous project, but this time, it will run over a physical network cable.

By the end of this tutorial, you will know how to:

* Re-route the ESP32's hardware SPI pins to communicate with the W5500.
* Hard-reset an external Ethernet controller using GPIO.
* Request a dynamic IP address from your router using DHCP.
* Serve a web-based control panel over a physical LAN network.

Requirements
============

Before starting, make sure you have the following:

* The STEAM development kit
* An active Ethernet cable connected to your home router or network switch
* USB cable
* Arduino IDE with ESP32 toolchain installed
* **Ethernet** library installed (Go to **Tools → Manage Libraries**, search for "Ethernet" by various/Arduino, and install it).

In this project, the built-in LED on **GPIO08** will act as our relay/switch indicator. 

.. figure:: ../../img/MCU_board.png
   :align: center
   :width: 400
   :figclass: align-center

   The interfacing description of the microcontroller board 

The W5500 chip communicates with the ESP32 via SPI (Serial Peripheral Interface) using the following pin mapping:

* **CLK (Clock):** GPIO13
* **MISO (Master In Slave Out):** GPIO12
* **MOSI (Master Out Slave In):** GPIO11 *(Verify this pin with your board schematic)*
* **CS (Chip Select):** GPIO14
* **RESET:** GPIO9
* **INT (Interrupt):** GPIO10 *(Not used in this basic polling example)*

Writing the Program
===================

Create a new sketch in the Arduino IDE and enter the following code:

.. code-block:: cpp

    #include <SPI.h>
    #include <Ethernet.h>

    // Define the pin acting as our digital switch
    #define RELAY_PIN 8 

    // W5500 Pin Mapping
    #define W5500_CS    14
    #define W5500_RST   9
    #define SPI_SCK     13
    #define SPI_MISO    12
    #define SPI_MOSI    11 // Update if your schematic uses a different MOSI pin

    // Provide a MAC address for the Ethernet controller
    byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

    // Instantiate the server on standard HTTP port 80
    EthernetServer server(80);

    void setup() {
        Serial.begin(115200);
        
        pinMode(RELAY_PIN, OUTPUT);
        digitalWrite(RELAY_PIN, LOW); // Start with switch OFF

        // 1. Hard Reset the W5500 chip
        Serial.println("\nResetting W5500...");
        pinMode(W5500_RST, OUTPUT);
        digitalWrite(W5500_RST, LOW);
        delay(10); // Hold reset low
        digitalWrite(W5500_RST, HIGH);
        delay(100); // Give the chip time to boot up

        // 2. Re-route ESP32 SPI pins to match our hardware mapping
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, W5500_CS);
        
        // 3. Tell the Ethernet library which pin is used for Chip Select
        Ethernet.init(W5500_CS);

        // 4. Start the Ethernet connection using DHCP (auto-assign IP)
        Serial.println("Configuring Ethernet via DHCP...");
        if (Ethernet.begin(mac) == 0) {
            Serial.println("Failed to configure Ethernet using DHCP.");
            // If DHCP fails, you could halt here or assign a static IP manually
            while(true); 
        }
        
        // Retrieve and print the assigned IP address
        Serial.print("Server IP address: ");
        Serial.println(Ethernet.localIP());
        
        // Start the web server
        server.begin();
        Serial.println("Server started successfully!");
    }

    void loop() {
        // Check if a web browser (client) has connected to our server
        EthernetClient client = server.available();   

        if (client) {                             
            String currentLine = ""; 
            
            while (client.connected()) {            
                if (client.available()) {             
                    char c = client.read(); 
                    
                    if (c == '\n') {                    
                        // End of HTTP request header
                        if (currentLine.length() == 0) {
                            // Send standard HTTP response headers
                            client.println("HTTP/1.1 200 OK");
                            client.println("Content-type:text/html");
                            client.println("Connection: close");
                            client.println(); 

                            // Construct the HTML/CSS web page
                            client.print("<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
                            client.print("<style>body{font-family:Arial; text-align:center; margin-top:50px;}");
                            client.print(".btn{display:inline-block; padding:20px 40px; font-size:24px; text-decoration:none; color:#fff; border-radius:8px; margin:10px; font-weight:bold;}");
                            client.print(".on{background-color:#2ecc71;} .off{background-color:#e74c3c;}</style></head>");
                            
                            client.print("<body><h1>Ethernet Relay Control</h1>");
                            
                            // Dynamically update status label
                            if (digitalRead(RELAY_PIN) == HIGH) {
                                client.print("<p>Current State: <span style='color:#2ecc71; font-weight:bold;'>ON</span></p>");
                            } else {
                                client.print("<p>Current State: <span style='color:#e74c3c; font-weight:bold;'>OFF</span></p>");
                            }
                            
                            // Hyperlinked control buttons
                            client.print("<a href=\"/ON\"><div class='btn on'>TURN ON</div></a>");
                            client.print("<a href=\"/OFF\"><div class='btn off'>TURN OFF</div></a>");
                            client.print("</body></html>");

                            break; 
                        } else {    
                            currentLine = ""; 
                        }
                    } else if (c != '\r') {  
                        currentLine += c; 
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
            // Disconnect client connection
            client.stop();
        }
    }


Uploading the Program
^^^^^^^^^^^^^^^^^^^^^

1. Plug an active Ethernet cable into the board's RJ45 port.
2. Connect the board to your computer using the USB cable.
3. Open the **Serial Monitor** (**Tools → Serial Monitor**) and set the baud rate to **115200**.
4. Select your board and port, then click **Upload**.

Expected Result
^^^^^^^^^^^^^^^

* Watch the Serial Monitor. After a brief pause, the ESP32 will successfully negotiate with your router and output a message like: ``Server IP address: 192.168.1.45``.
* Open a web browser on any device connected to the **same network router** (it can be on Wi-Fi, as long as it routes to the same network).
* Type the IP address from the Serial Monitor into the URL bar and press Enter.
* The control panel will load instantly. Clicking the ON/OFF buttons will toggle the local GPIO08 LED via your physical Ethernet connection.

How the Code Works
^^^^^^^^^^^^^^^^^^

**Hardware Resetting**

External communication chips can sometimes get stuck in unpredictable states during power-up. We ensure a clean slate by briefly pulling the W5500's ``RESET`` pin LOW. 

.. code-block:: cpp

    digitalWrite(W5500_RST, LOW);
    delay(10);
    digitalWrite(W5500_RST, HIGH);

This is a best practice in hardware design, forcing the chip to reboot exactly when the ESP32 is ready to talk to it.

**Custom SPI Routing**

The ESP32 allows us to route internal data buses to almost any physical pin. 

.. code-block:: cpp

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, W5500_CS);
    Ethernet.init(W5500_CS);

First, we tell the standard Arduino ``SPI`` library to use our specific IO pins. Next, we tell the ``Ethernet`` library which of those pins is the Chip Select (CS) line, so it knows how to "wake up" the W5500 when it wants to send data.

**DHCP (Dynamic Host Configuration Protocol)**

Unlike our Wi-Fi project where we hosted our own network, here we are joining an existing one. Calling ``Ethernet.begin(mac)`` with no other parameters tells the router: *"Hello, I am a new device with this MAC address. Please assign me an available IP address."* The router responds with the IP, which we read using ``Ethernet.localIP()``.

Experiment
^^^^^^^^^^

* **Static IP Address:** Depending on router settings, DHCP IP addresses can change over time. Look up the Arduino Ethernet library documentation to find out how to pass a fixed ``IPAddress`` variable into ``Ethernet.begin()`` so your board always boots up with the exact same IP.
* **Auto-Refresh Page:** HTML has a built-in tag to automatically refresh the page. Add ``client.print("<meta http-equiv=\"refresh\" content=\"5\">");`` to your HTML header block, and watch the page automatically update its status every 5 seconds without you needing to press anything!

Summary
=======
In this tutorial, you bridged an external networking controller (W5500) to the ESP32 using the SPI bus. You initialized hardware using reset pins, re-routed standard communication lines, and successfully requested a network identity from a DHCP router. 

Key concepts covered include:

* Managing external IC reset sequences.
* Remapping ESP32 SPI hardware pins (`SPI.begin`).
* Understanding MAC addresses and DHCP IP negotiation.
* Reusing generic Web Server parsing logic seamlessly across different transport layers (Wi-Fi vs. Ethernet).