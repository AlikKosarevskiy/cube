#include <Arduino.h>
#include <driver/i2s.h>
#include <FastLED.h>
#include <math.h>

// =====================================
// I2S MICROPHONE
// =====================================

#define I2S_SCK 26
#define I2S_WS  25
#define I2S_SD  32

#define SAMPLE_RATE 16000
#define BUFFER_LEN 256

int32_t buffer[BUFFER_LEN * 2];

// =====================================
// OUTPUT2
// =====================================

#define OUTPUT2_PIN 2

// =====================================
// WS2812B LED STRIP
// =====================================

#define LED_STRIP_PIN 18//4
#define NUM_LEDS 16

#define LED_BRIGHTNESS 80

CRGB leds[NUM_LEDS];

// =====================================
// SOUND DETECTION
// =====================================

#define COOLDOWN_MS 250
#define LED_TIME_MS 1000

const int32_t SOUND_LEVELS[8] = {
    400000,
    600000,
    800000,
    1000000,
    1200000,
    1400000,
    1600000,
    1800000
};

// =====================================
// TIMERS
// =====================================

unsigned long lastTrigger = 0;
unsigned long ledOffTime = 0;

uint32_t eventCounter = 0;

// =====================================
// SETUP
// =====================================

void setup() {

    Serial.begin(115200);
    delay(1000);

    pinMode(OUTPUT2_PIN, OUTPUT);
    digitalWrite(OUTPUT2_PIN, LOW);

    // Initialize LED strip

    FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>(
        leds,
        NUM_LEDS
    );

    FastLED.setBrightness(LED_BRIGHTNESS);

    FastLED.clear();
    FastLED.show();

    Serial.println("LED strip test started");

// Sequential LED test

for (int i = 0; i < 8; i++) {

    FastLED.clear();

    // Two LEDs for each level

    leds[i * 2]     = CRGB::Green;
    leds[i * 2 + 1] = CRGB::Green;

    FastLED.show();

    Serial.printf("Testing LED pair %d + %d\n",
                  i * 2 + 1,
                  i * 2 + 2);

    delay(400);
}

// Turn everything off

FastLED.clear();
FastLED.show();

Serial.println("LED strip test completed");

    // =================================
    // I2S CONFIGURATION
    // =================================

    i2s_config_t config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };

    esp_err_t result = i2s_driver_install(
        I2S_NUM_0,
        &config,
        0,
        NULL
    );

    if (result != ESP_OK) {
        Serial.printf("I2S driver error: %d\n", result);
        while (true) delay(1000);
    }

    result = i2s_set_pin(I2S_NUM_0, &pins);

    if (result != ESP_OK) {
        Serial.printf("I2S pin error: %d\n", result);
        while (true) delay(1000);
    }

    i2s_zero_dma_buffer(I2S_NUM_0);

    Serial.println();
    Serial.println("==============================");
    Serial.println(" SOUND LEVEL DETECTOR");
    Serial.println(" INMP441 + WS2812B");
    Serial.println("==============================");
    Serial.println("I2S started");
    Serial.println("LED strip initialized");
}


// =====================================
// DETERMINE SOUND LEVEL
// =====================================

int getSoundLevel(int32_t peak) {

    int level = 0;

    for (int i = 0; i < 8; i++) {

        if (peak >= SOUND_LEVELS[i]) {
            level = i + 1;
        }
    }

    return level;
}


// =====================================
// DISPLAY SOUND LEVEL
// =====================================

void showSoundLevel(int level) {

    FastLED.clear();

    if (level < 1 || level > 8) {
        FastLED.show();
        return;
    }

    // Two LEDs per level

    int firstLed = (level - 1) * 2;

    // Different color for each level

    CHSV color(
        (level - 1) * 32,
        255,
        255
    );

    leds[firstLed] = color;
    leds[firstLed + 1] = color;

    FastLED.show();
}


// =====================================
// TURN OFF OUTPUTS
// =====================================

void turnOffOutputs() {

    digitalWrite(OUTPUT2_PIN, LOW);

    FastLED.clear();
    FastLED.show();

    ledOffTime = 0;
}


// =====================================
// MAIN LOOP
// =====================================

void loop() {

    size_t bytes_read = 0;

    esp_err_t result = i2s_read(
        I2S_NUM_0,
        buffer,
        sizeof(buffer),
        &bytes_read,
        portMAX_DELAY
    );

    if (result != ESP_OK || bytes_read == 0)
        return;

    int frames = bytes_read / (2 * sizeof(int32_t));

    // =================================
    // CALCULATE AVERAGE
    // =================================

    int64_t sum = 0;

    for (int i = 0; i < frames; i++) {

        int32_t sample = buffer[i * 2] >> 8;

        sum += sample;
    }

    int32_t average = sum / frames;

    // =================================
    // CALCULATE PEAK
    // =================================

    int32_t peak = 0;

    for (int i = 0; i < frames; i++) {

        int32_t sample =
            (buffer[i * 2] >> 8) - average;

        int32_t magnitude = abs(sample);

        if (magnitude > peak)
            peak = magnitude;
    }

    unsigned long now = millis();

    // =================================
    // SOUND EVENT DETECTION
    // =================================

    int soundLevel = getSoundLevel(peak);

    if (soundLevel > 0 &&
        now - lastTrigger >= COOLDOWN_MS) {

        lastTrigger = now;

        ledOffTime = now + LED_TIME_MS;

        eventCounter++;

        // Activate OUTPUT2

        digitalWrite(OUTPUT2_PIN, HIGH);

        // Display corresponding LED pair

        showSoundLevel(soundLevel);

        Serial.printf(
            "EVENT: %lu | LEVEL: %d | PEAK: %ld\n",
            (unsigned long)eventCounter,
            soundLevel,
            (long)peak
        );
    }

    // =================================
    // TURN OFF AFTER LED_TIME_MS
    // =================================

    if (ledOffTime != 0 &&
        (long)(now - ledOffTime) >= 0) {

        turnOffOutputs();
    }

    // =================================
    // SERIAL PLOTTER
    // =================================

    Serial.printf(">PEAK:%ld\r\n", (long)peak);
}
