#define BUZZER_PIN 3
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
