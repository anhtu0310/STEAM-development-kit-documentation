*************************************************************
Project 10 : Environmental Monitoring with BME280 and OLED
*************************************************************

Introduction
============ 

In this project, we will combine the SH1106 OLED display from Project 5 with the **BME280** sensor. The BME280 is a sophisticated environmental sensor capable of measuring temperature, humidity, and barometric pressure.

Because the BME280 and the SH1106 OLED both use the **I2C interface**, they can be connected to the same two pins (GPIO48 and GPIO47) simultaneously. By the end of this tutorial, you will know how to:

* Manage multiple devices on a single I2C bus.
* Read environmental data from the BME280 using the Adafruit library.
* Update an OLED display in real-time with live sensor data.

Requirements
============

* The STEAM development kit or The STEAM standalone microcontroller board 
* USB cable
* Arduino IDE with ESP32 toolchain installed
* **Adafruit BME280** and **Adafruit Unified Sensor** libraries installed via Library Manager.

.. note::
    **Bus Sharing:** I2C devices use unique "addresses" to communicate. If you encounter errors, verify that your sensor is visible on the bus. The standard address for a BME280 is usually `0x76` or `0x77`, while the SH1106 is typically `0x3C`.



Writing the Program
===================

Create a new sketch in the Arduino IDE and enter the following code:

.. code-block:: cpp

    #include <Wire.h>
    #include <Adafruit_Sensor.h>
    #include <Adafruit_BME280.h>
    #include <U8g2lib.h>

    #define SDA_PIN 47
    #define SCL_PIN 48

    Adafruit_BME280 bme;
    U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

    void setup() {
        Serial.begin(115200);
        Wire.begin(SDA_PIN, SCL_PIN);

        // Initialize Display
        u8g2.begin();

        // Initialize Sensor (Checking address 0x76)
        if (!bme.begin(0x76)) {
            Serial.println("Could not find a valid BME280 sensor!");
            while (1);
        }
    }

    void loop() {
        // Read sensor values
        float temp = bme.readTemperature();
        float hum = bme.readHumidity();
        float pres = bme.readPressure() / 100.0F; // Convert Pa to hPa

        // Update Display
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        
        u8g2.drawStr(0, 15, "Temp: ");
        u8g2.print(temp); u8g2.print(" C");

        u8g2.drawStr(0, 35, "Hum: ");
        u8g2.print(hum); u8g2.print(" %");

        u8g2.drawStr(0, 55, "Pres: ");
        u8g2.print(pres); u8g2.print(" hPa");

        u8g2.sendBuffer();
        delay(1000); // Update data once per second
    }

Uploading the Program
^^^^^^^^^^^^^^^^^^^^^

1. Ensure both libraries (Adafruit BME280 and U8g2) are installed.
2. Select your board and port, and click **Upload**.

Expected Result
^^^^^^^^^^^^^^^

* Once the code is uploaded, the OLED will display your current room temperature, humidity, and barometric pressure.
* The readings will refresh once every second.

How the Code Works
^^^^^^^^^^^^^^^^^^

**The I2C Advantage**

In the `setup()` function, `Wire.begin(SDA_PIN, SCL_PIN)` initializes the bus. Both the BME280 library and the U8g2 library automatically hook into this existing bus. When the code requests data from the BME280, it sends a command specifically addressed to the sensor; when it updates the display, it sends commands addressed specifically to the OLED.

**Data Formatting**

We use `u8g2.print()` to dynamically convert our sensor float variables into text that the display can understand. This is a very efficient way to display live data compared to writing individual strings for every digit.

Experiment
^^^^^^^^^^

* **Add a Warning:** Use an `if` statement to check if the temperature exceeds a certain value (e.g., 30°C). If it does, print "TOO HOT!" on the OLED.
* **I2C Scanner:** If you have trouble getting the sensor to work, look up an "I2C Scanner" sketch online. It will scan the I2C bus and report the hex addresses of all connected devices, which helps identify if the sensor is on `0x76` or `0x77`.

Summary
=======
In this tutorial, you learned the true power of the I2C communication protocol: bus sharing. By connecting both a sensor and a display to the same two pins, you created a functional environmental monitor.

Key concepts covered include:

* Sharing I2C pins across multiple devices.
* Understanding I2C addresses.
* Integrating sensor libraries (BME280) with graphics libraries (U8g2).
* Real-time data visualization on embedded displays.