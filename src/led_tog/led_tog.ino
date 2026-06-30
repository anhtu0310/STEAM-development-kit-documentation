#define BUZZER_PIN 9
#define BUT_PIN 0

bool buzzer_sts = false;
bool last_but_sts = HIGH;

void setup() {
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUT_PIN, INPUT);

    // Ensure the buzzer is off to start
    digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
    // Read the current state of the button
    bool current_but_sts = digitalRead(BUT_PIN);

    // Check if the button transitioned from HIGH (unpressed) to LOW (pressed)
    if (last_but_sts == HIGH && current_but_sts == LOW) {
        buzzer_sts = !buzzer_sts;               // Toggle the buzzer status
        digitalWrite(BUZZER_PIN, buzzer_sts);   // Apply the new status
        delay(50);                              // Small delay to debounce
    }

    // Save the current state for the next loop iteration
    last_but_sts = current_but_sts;
}
