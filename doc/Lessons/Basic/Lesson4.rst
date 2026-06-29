*************************************************************
Project 4 : Play a Melody using a Button
*************************************************************

Introduction
============ 

In this project, we will add a fun twist to our button logic: triggering a song! Instead of simply turning the buzzer ON and OFF, pressing the button will now command the microcontroller to play a short melody. By the end of this tutorial, you will know how to:

* Store musical notes and durations using Arrays.
* Use a loop to iterate through data.
* Generate audio frequencies using the ``tone()`` function.

Requirements
============

* The STEAM development kit or The STEAM standalone microcontroller board 
* USB cable
* Arduino IDE with ESP32 toolchain installed

.. note::
    **Hardware Tip:** To hear distinct musical pitches, a **passive buzzer** or speaker is required. If your board has an **active buzzer** (which makes a continuous sound when powered), this code will still work, but you will only hear the *rhythm* of the notes as clicks or beeps, rather than different pitches.

Writing the Program
===================

Create a new sketch in the Arduino IDE and enter the following code. We will be programming the classic "Twinkle Twinkle Little Star".

.. code-block:: cpp

    #define BUZZER_PIN 9
    #define BUT_PIN 0

    // Define the frequencies for musical notes
    #define NOTE_C4  262
    #define NOTE_D4  294
    #define NOTE_E4  330
    #define NOTE_F4  349
    #define NOTE_G4  392
    #define NOTE_A4  440

    // Array storing the sequence of notes
    int melody[] = { NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4 };
    
    // Array storing note durations (4 = quarter note, 2 = half note)
    int durations[] = { 4, 4, 4, 4, 4, 4, 2 }; 

    bool last_but_sts = HIGH; 

    void setup() {
        pinMode(BUZZER_PIN, OUTPUT);
        pinMode(BUT_PIN, INPUT);
    }

    void loop() {
        // Read the current state of the active-LOW button
        bool current_but_sts = digitalRead(BUT_PIN);

        // Check for a button press (transition from HIGH to LOW)
        if (last_but_sts == HIGH && current_but_sts == LOW) {
            playSong(); // Trigger the melody function
        }
        
        // Save state and briefly pause to debounce
        last_but_sts = current_but_sts;
        delay(10); 
    }

    // Custom function to play the melody array
    void playSong() {
        // Loop through all 7 notes in our arrays
        for (int i = 0; i < 7; i++) {
            // Calculate note duration in milliseconds (1 second = 1000ms)
            int noteDuration = 1000 / durations[i];
            
            // Play the note on the buzzer pin
            tone(BUZZER_PIN, melody[i], noteDuration);
            
            // Add a short pause between notes so they don't blend together
            int pauseBetweenNotes = noteDuration * 1.30;
            delay(pauseBetweenNotes);
            
            // Stop the tone before the next note plays
            noTone(BUZZER_PIN);
        }
    }


Uploading the Program
^^^^^^^^^^^^^^^^^^^^^

1. Connect the board to your computer.
2. Select **ESP32S3 Dev Module** from **Tools → Board**.
3. Select the correct serial port.
4. Click **Upload**.

Expected Result
^^^^^^^^^^^^^^^

* The system starts silently.
* When you press the button, the board plays the first line of "Twinkle Twinkle Little Star".
* Because of the state change detection, holding the button down will only play the song once. 
* You must wait for the song to finish, release the button, and press it again to hear it a second time.

How the Code Works
^^^^^^^^^^^^^^^^^^

**Storing the Melody (Arrays)**

We use two arrays to store the data for our song: one for the pitch (frequency) and one for the timing (duration).

.. code-block:: cpp

    int melody[] = { NOTE_C4, NOTE_C4, NOTE_G4... };
    int durations[] = { 4, 4, 4... }; 

Arrays are like lists. The microcontroller will read through these lists one by one to know exactly what to play and for how long.

**The `playSong()` Function**

Instead of cluttering our `loop()` with audio commands, we created a custom function. Inside, a `for` loop counts from `0` to `6` (giving us 7 steps for our 7 notes). 

.. code-block:: cpp

    tone(BUZZER_PIN, melody[i], noteDuration);

The ``tone()`` function tells the microcontroller to rapidly pulse the pin at a specific frequency, creating a sound wave. The ``melody[i]`` grabs the current note from our array, and ``noteDuration`` tells it how long to hold it.

Experiment
^^^^^^^^^^

* **Change the Tempo:** Find the line ``int noteDuration = 1000 / durations[i];``. Try changing `1000` to `500` to play the song twice as fast!
* **Write Your Own Song:** Look up the frequencies for different musical notes online and modify the ``melody[]`` array to play the Super Mario theme or a custom tune. Just make sure the number of items in both the ``melody[]`` and ``durations[]`` arrays match, and update the ``for`` loop limit accordingly.

Summary
=======
In this project, you stepped beyond basic HIGH and LOW digital signals and learned how to use the ``tone()`` function to generate specific frequencies. You also learned how to use arrays and a ``for`` loop to organize and iterate through sequences of data. This combination of button edge-detection and data processing is a powerful foundation for building interactive embedded systems.