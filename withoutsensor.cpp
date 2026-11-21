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
// RANGE SELECTION
// =====================================

#define RANGE_PIN_1 16
#define RANGE_PIN_2 17

// =====================================
// WS2812B LED STRIP
// =====================================

#define LED_STRIP_PIN 18
#define NUM_LEDS 16

#define LED_BRIGHTNESS 80

CRGB leds[NUM_LEDS];

// =====================================
// TIMING
// =====================================

#define COOLDOWN_MS 250

#define RESULT_TIME_MS 10000

#define BREATH_UPDATE_MS 20

// =====================================
// SOUND EVENT DETECTION
// =====================================

// Minimum RMS for a block to be considered active.
// This is a starting value and may need calibration.

#define SOUND_ACTIVITY_THRESHOLD 70000

// Minimum number of active blocks required.
// 18 blocks x approximately 16 ms = 288 ms.

#define MIN_ACTIVE_BLOCKS 18

// Event is finished after 300 ms of silence.

#define SILENCE_REARM_MS 300

// Approximate duration of one I2S block.

#define BLOCK_TIME_MS ((BUFFER_LEN * 1000UL) / SAMPLE_RATE)

// Number of consecutive silent blocks required.

#define SILENCE_REARM_BLOCKS \
    ((SILENCE_REARM_MS + BLOCK_TIME_MS - 1) / BLOCK_TIME_MS)

// =====================================
// SOUND LEVEL THRESHOLDS
// These are RMS values, NOT PEAK values.
// They are initial calibration values.
// =====================================

// RANGE 1: Standard

const int32_t SOUND_LEVELS_1[8] = {
    50000,
    90000,
    130000,
    170000,
    210000,
    250000,
    290000,
    330000
};

// RANGE 2: Wide

const int32_t SOUND_LEVELS_2[8] = {
    50000,
    120000,
    190000,
    260000,
    330000,
    400000,
    470000,
    540000
};

// RANGE 3: Extended

const int32_t SOUND_LEVELS_3[8] = {
    30000,
    150000,
    270000,
    390000,
    510000,
    630000,
    750000,
    870000
};

// =====================================
// LED MODES
// =====================================

enum LedMode {
    LED_BREATHING,
    LED_RESULT
};

LedMode currentLedMode = LED_BREATHING;

// =====================================
// TIMERS
// =====================================

unsigned long lastTrigger = 0;

unsigned long resultStartTime = 0;

unsigned long lastBreathUpdate = 0;

uint32_t eventCounter = 0;

// =====================================
// BREATHING PARAMETERS
// =====================================

uint8_t colorPhase = 0;

// =====================================
// CURRENT RANGE
// =====================================

uint8_t currentRange = 1;

const int32_t* activeThresholds = SOUND_LEVELS_1;

// =====================================
// SOUND EVENT ACCUMULATOR
// =====================================

bool soundInProgress = false;

uint32_t activeBlocks = 0;

uint32_t silenceBlocks = 0;

uint64_t rmsSum = 0;


// =====================================
// SELECT SOUND RANGE
// =====================================

void selectSoundRange() {

    bool pin1 = digitalRead(RANGE_PIN_1);
    bool pin2 = digitalRead(RANGE_PIN_2);

    if (!pin1 && pin2) {

        currentRange = 2;
        activeThresholds = SOUND_LEVELS_2;

    }
    else if (pin1 && !pin2) {

        currentRange = 3;
        activeThresholds = SOUND_LEVELS_3;

    }
    else {

        currentRange = 1;
        activeThresholds = SOUND_LEVELS_1;

    }
}


// =====================================
// DETERMINE SOUND LEVEL
// =====================================

int getSoundLevel(int32_t rms) {

    int level = 0;

    for (int i = 0; i < 8; i++) {

        if (rms >= activeThresholds[i]) {
            level = i + 1;
        }
    }

    return level;
}


// =====================================
// DISPLAY SOUND RESULT
// =====================================

void showSoundLevel(int level) {

    FastLED.clear();

    if (level < 1 || level > 8) {

        FastLED.show();
        return;
    }

    // Two LEDs for each level.

    int firstLed = (level - 1) * 2;

    // White result indication.

    leds[firstLed] = CRGB::White;
    leds[firstLed + 1] = CRGB::White;

    FastLED.show();
}


// =====================================
// BREATHING EFFECT
// =====================================

void updateBreathing() {

    unsigned long now = millis();

    if (now - lastBreathUpdate < BREATH_UPDATE_MS)
        return;

    lastBreathUpdate = now;

    // Smooth brightness oscillation.

    uint8_t brightness = beatsin8(
        18,
        20,
        255
    );

    // Slowly changing color.

    colorPhase++;

    uint8_t hue = colorPhase;

    // Apply color and brightness to all LEDs.

    for (int i = 0; i < NUM_LEDS; i++) {

        leds[i] = CHSV(
            hue,
            200,
            brightness
        );
    }

    FastLED.show();
}


// =====================================
// START BREATHING
// =====================================

void startBreathing() {

    currentLedMode = LED_BREATHING;

    lastBreathUpdate = 0;

    updateBreathing();
}


// =====================================
// START SOUND RESULT
// =====================================

void startSoundResult(int level) {

    currentLedMode = LED_RESULT;

    resultStartTime = millis();

    // Activate OUTPUT2.

    digitalWrite(OUTPUT2_PIN, HIGH);

    // Display corresponding white LED pair.

    showSoundLevel(level);
}


// =====================================
// TURN OFF OUTPUTS
// =====================================

void turnOffOutputs() {

    digitalWrite(OUTPUT2_PIN, LOW);

    startBreathing();
}


// =====================================
// RESET SOUND EVENT
// =====================================

void resetSoundEvent() {

    soundInProgress = false;

    activeBlocks = 0;

    silenceBlocks = 0;

    rmsSum = 0;
}


// =====================================
// FINISH SOUND EVENT
// =====================================

void finishSoundEvent() {

    // Calculate average RMS of active blocks.

    int32_t averageRMS = 0;

    if (activeBlocks > 0) {

        averageRMS = rmsSum / activeBlocks;
    }

    // Check minimum sound duration.

    if (activeBlocks >= MIN_ACTIVE_BLOCKS) {

        unsigned long now = millis();

        int soundLevel = getSoundLevel(averageRMS);

        if (soundLevel > 0 &&
            now - lastTrigger >= COOLDOWN_MS) {

            lastTrigger = now;

            eventCounter++;

            startSoundResult(soundLevel);

            Serial.printf(
                "EVENT: %lu | RANGE: %d | LEVEL: %d | RMS: %ld | ACTIVE BLOCKS: %lu\n",
                (unsigned long)eventCounter,
                currentRange,
                soundLevel,
                (long)averageRMS,
                (unsigned long)activeBlocks
            );

        }
        else {

            Serial.printf(
                "SOUND BELOW LEVELS | RMS: %ld | ACTIVE BLOCKS: %lu\n",
                (long)averageRMS,
                (unsigned long)activeBlocks
            );
        }

    }
    else {

        // Short sounds are ignored.

        Serial.printf(
            "IGNORED SHORT SOUND | ACTIVE BLOCKS: %lu\n",
            (unsigned long)activeBlocks
        );
    }

    resetSoundEvent();
}


// =====================================
// PROCESS SOUND BLOCK
// =====================================

void processSoundBlock(int32_t rms) {

    if (rms >= SOUND_ACTIVITY_THRESHOLD) {

        // Sound detected.

        soundInProgress = true;

        activeBlocks++;

        rmsSum += (uint32_t)rms;

        // Reset silence counter.

        silenceBlocks = 0;
    }

    else if (soundInProgress) {

        // No sound in current block.

        silenceBlocks++;

        // Check if the sound event has ended.

        if (silenceBlocks >= SILENCE_REARM_BLOCKS) {

            finishSoundEvent();
        }
    }
}


// =====================================
// CALCULATE RMS
// =====================================

int32_t calculateRMS(int frames) {

    if (frames <= 0)
        return 0;

    // Calculate average.

    int64_t sum = 0;

    for (int i = 0; i < frames; i++) {

        int32_t sample = buffer[i * 2] >> 8;

        sum += sample;
    }

    int32_t average = sum / frames;

    // Calculate sum of squared samples.

    uint64_t sumSquares = 0;

    for (int i = 0; i < frames; i++) {

        int32_t sample =
            (buffer[i * 2] >> 8) - average;

        int64_t value = sample;

        sumSquares += (uint64_t)(value * value);
    }

    // Calculate RMS.

    double meanSquare =
        (double)sumSquares / frames;

    int32_t rms = (int32_t)sqrt(meanSquare);

    return rms;
}


// =====================================
// SETUP
// =====================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    // OUTPUT2.

    pinMode(OUTPUT2_PIN, OUTPUT);

    digitalWrite(OUTPUT2_PIN, LOW);

    // Range selection inputs.

    pinMode(RANGE_PIN_1, INPUT_PULLUP);

    pinMode(RANGE_PIN_2, INPUT_PULLUP);

    selectSoundRange();

    // LED strip initialization.

    FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>(
        leds,
        NUM_LEDS
    );

    FastLED.setBrightness(LED_BRIGHTNESS);

    FastLED.clear();

    FastLED.show();

    // =================================
    // LED TEST
    // =================================

    Serial.println("LED strip test started");

    for (int i = 0; i < 8; i++) {

        FastLED.clear();

        leds[i * 2] = CRGB::Green;

        leds[i * 2 + 1] = CRGB::Green;

        FastLED.show();

        Serial.printf(
            "Testing LED pair %d + %d\n",
            i * 2 + 1,
            i * 2 + 2
        );

        delay(400);
    }

    FastLED.clear();

    FastLED.show();

    Serial.println("LED strip test completed");

    // =================================
    // I2S CONFIGURATION
    // =================================

    i2s_config_t config = {

        .mode = (i2s_mode_t)(
            I2S_MODE_MASTER | I2S_MODE_RX
        ),

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

        Serial.printf(
            "I2S driver error: %d\n",
            result
        );

        while (true) delay(1000);
    }

    result = i2s_set_pin(
        I2S_NUM_0,
        &pins
    );

    if (result != ESP_OK) {

        Serial.printf(
            "I2S pin error: %d\n",
            result
        );

        while (true) delay(1000);
    }

    i2s_zero_dma_buffer(I2S_NUM_0);

    // =================================
    // STARTUP INFORMATION
    // =================================

    Serial.println();

    Serial.println("==============================");

    Serial.println(" SOUND LEVEL DETECTOR");

    Serial.println(" INMP441 + WS2812B");

    Serial.println("==============================");

    Serial.println("I2S started");

    Serial.println("LED strip initialized");

    Serial.printf(
        "Selected sound range: %d\n",
        currentRange
    );

    Serial.printf(
        "Activity threshold: %d\n",
        SOUND_ACTIVITY_THRESHOLD
    );

    Serial.printf(
        "Minimum active blocks: %d\n",
        MIN_ACTIVE_BLOCKS
    );

    Serial.printf(
        "Silence blocks: %d\n",
        SILENCE_REARM_BLOCKS
    );

    Serial.println("System ready");

    // Start breathing animation.

    startBreathing();
}


// =====================================
// MAIN LOOP
// =====================================

void loop() {

    // =================================
    // READ MICROPHONE
    // =================================

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

    if (frames == 0)
        return;

    // =================================
    // CALCULATE RMS
    // =================================

    int32_t rms = calculateRMS(frames);

    unsigned long now = millis();

    // =================================
    // UPDATE SOUND RANGE
    // =================================

    static uint8_t previousRange = 0;

    selectSoundRange();

    if (currentRange != previousRange) {

        previousRange = currentRange;

        Serial.printf(
            "SOUND RANGE CHANGED: %d\n",
            currentRange
        );
    }

    // =================================
    // PROCESS SOUND EVENT
    // =================================

    processSoundBlock(rms);

    // =================================
    // RESULT TIMER
    // =================================

    if (currentLedMode == LED_RESULT) {

        if (now - resultStartTime >= RESULT_TIME_MS) {

            turnOffOutputs();
        }
    }

    // =================================
    // BREATHING ANIMATION
    // =================================

    if (currentLedMode == LED_BREATHING) {

        updateBreathing();
    }

    // =================================
    // SERIAL OUTPUT
    // =================================

    Serial.printf(
        ">RMS:%ld\n",
        (long)rms
    );
}
