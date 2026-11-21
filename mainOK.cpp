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
// HC-SR04 ULTRASONIC SENSOR
// =====================================

#define TRIG_PIN 19
#define ECHO_PIN 23

// Обнаружение предмета
#define OBJECT_DISTANCE_ON 80.0

// Освобождение зоны (гистерезис)
#define OBJECT_DISTANCE_OFF 90.0

#define DISTANCE_UPDATE_MS 60

#define ECHO_TIMEOUT_US 3000

bool objectPresent = false;

float objectDistance = 0;

unsigned long lastDistanceUpdate = 0;

uint8_t farReadings = 0;


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

#define SOUND_ACTIVITY_THRESHOLD 70000

#define MIN_ACTIVE_BLOCKS 18

#define SILENCE_REARM_MS 300

#define BLOCK_TIME_MS ((BUFFER_LEN * 1000UL) / SAMPLE_RATE)

#define SILENCE_REARM_BLOCKS \
    ((SILENCE_REARM_MS + BLOCK_TIME_MS - 1) / BLOCK_TIME_MS)


// =====================================
// SOUND LEVEL THRESHOLDS
// =====================================

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
    LED_RESULT,
    LED_GREEN
};

LedMode currentLedMode = LED_BREATHING;


// =====================================
// SYSTEM STATES
// =====================================

// Разрешён ли анализ звука
bool soundAnalysisEnabled = false;

// Ожидание удаления предмета после результата
bool waitForObjectClear = false;


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
// FUNCTION PROTOTYPES
// =====================================

void resetSoundEvent();
void startBreathing();
void finishResult();
void updateObjectDetection();
void processSoundBlock(int32_t rms);
void startSoundResult(int level);
void finishSoundEvent();
void updateBreathing();
void selectSoundRange();
int getSoundLevel(int32_t rms);
int32_t calculateRMS(int frames);
void showSoundLevel(int level);

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

    int firstLed = (level - 1) * 2;

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

    uint8_t brightness = beatsin8(
        18,
        20,
        255
    );

    colorPhase++;

    uint8_t hue = colorPhase;

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
// GREEN LIGHT - OBJECT DETECTED
// =====================================

void showObjectDetected() {

    currentLedMode = LED_GREEN;

    fill_solid(leds, NUM_LEDS, CRGB::Green);

    FastLED.show();

}

// =====================================
// START SOUND RESULT
// =====================================

void startSoundResult(int level) {

    currentLedMode = LED_RESULT;

    resultStartTime = millis();

    digitalWrite(OUTPUT2_PIN, HIGH);

    showSoundLevel(level);

    Serial.println();
    Serial.println("========== SOUND RESULT ==========");

    Serial.printf("LEVEL: %d\n", level);

    Serial.println("LED RESULT: ON");
    Serial.println("OUTPUT2: HIGH");

    Serial.println("==================================");

}


// =====================================
// FINISH RESULT DISPLAY
// =====================================

void finishResult() {

    digitalWrite(OUTPUT2_PIN, LOW);

    soundAnalysisEnabled = false;

    resetSoundEvent();

    // Если предмет всё ещё перед датчиком,
    // ждём его удаления перед новым запуском.

    waitForObjectClear = objectPresent;

    Serial.println();
    Serial.println("RESULT TIME FINISHED");

    Serial.println("OUTPUT2: LOW");

    Serial.println("RETURN TO BREATHING");

    if (waitForObjectClear) {

        Serial.println("WAITING FOR OBJECT REMOVAL");

    }
    else {

        Serial.println("WAITING FOR NEXT OBJECT");

    }

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

    int32_t averageRMS = 0;

    if (activeBlocks > 0) {

        averageRMS = rmsSum / activeBlocks;

    }

    if (activeBlocks >= MIN_ACTIVE_BLOCKS) {

        unsigned long now = millis();

        int soundLevel = getSoundLevel(averageRMS);

        if (
            soundLevel > 0 &&
            now - lastTrigger >= COOLDOWN_MS
        ) {

            lastTrigger = now;

            eventCounter++;

            Serial.printf(
                "EVENT: %lu | RANGE: %d | LEVEL: %d | RMS: %ld | ACTIVE BLOCKS: %lu\n",
                (unsigned long)eventCounter,
                currentRange,
                soundLevel,
                (long)averageRMS,
                (unsigned long)activeBlocks
            );

            startSoundResult(soundLevel);

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

        soundInProgress = true;

        activeBlocks++;

        rmsSum += (uint32_t)rms;

        silenceBlocks = 0;

    }

    else if (soundInProgress) {

        silenceBlocks++;

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

    int64_t sum = 0;

    for (int i = 0; i < frames; i++) {

        int32_t sample = buffer[i * 2] >> 8;

        sum += sample;

    }

    int32_t average = sum / frames;

    uint64_t sumSquares = 0;

    for (int i = 0; i < frames; i++) {

        int32_t sample =
            (buffer[i * 2] >> 8) - average;

        int64_t value = sample;

        sumSquares += (uint64_t)(value * value);

    }

    double meanSquare =
        (double)sumSquares / frames;

    int32_t rms = (int32_t)sqrt(meanSquare);

    return rms;

}


// =====================================
// UPDATE ULTRASONIC SENSOR
// =====================================

void updateObjectDetection() {

    unsigned long now = millis();

    if (now - lastDistanceUpdate < DISTANCE_UPDATE_MS) {

        return;

    }

    lastDistanceUpdate = now;


    // Trigger pulse

    digitalWrite(TRIG_PIN, LOW);

    delayMicroseconds(2);

    digitalWrite(TRIG_PIN, HIGH);

    delayMicroseconds(10);

    digitalWrite(TRIG_PIN, LOW);


    // Read echo

    unsigned long duration = pulseIn(
        ECHO_PIN,
        HIGH,
        ECHO_TIMEOUT_US
    );


    // ---------------------------------
    // NO ECHO
    // ---------------------------------

    if (duration == 0) {

        if (objectPresent) {

            farReadings++;

            if (farReadings >= 3) {

                objectPresent = false;

                objectDistance = 0;

                farReadings = 0;

                Serial.println("OBJECT REMOVED");

            }
        }

        return;

    }


    // ---------------------------------
    // CALCULATE DISTANCE
    // ---------------------------------

    objectDistance = duration * 0.0343 / 2.0;

    Serial.printf(
    "DISTANCE: %.1f cm | ON: %.1f cm | OFF: %.1f cm\n",
    objectDistance,
    (float)OBJECT_DISTANCE_ON,
    (float)OBJECT_DISTANCE_OFF
);

    // ---------------------------------
    // OBJECT ENTERS DETECTION ZONE
    // ---------------------------------

    if (!objectPresent) {

        if (objectDistance <= OBJECT_DISTANCE_ON) {

            objectPresent = true;

            farReadings = 0;

            Serial.printf(
                "OBJECT DETECTED: %.1f cm\n",
                objectDistance
            );

        }

    }


    // ---------------------------------
    // OBJECT IS INSIDE DETECTION ZONE
    // ---------------------------------

    else {

        if (objectDistance > OBJECT_DISTANCE_OFF) {

            farReadings++;

            if (farReadings >= 2) {

                objectPresent = false;

                farReadings = 0;

                Serial.printf(
                    "OBJECT REMOVED: %.1f cm\n",
                    objectDistance
                );

            }

        }
        else {

            farReadings = 0;

        }

    }

}


// =====================================
// SETUP
// =====================================

void setup() {

    Serial.begin(115200);

    delay(1000);


    // ---------------------------------
    // OUTPUT2
    // ---------------------------------

    pinMode(OUTPUT2_PIN, OUTPUT);

    digitalWrite(OUTPUT2_PIN, LOW);


    // ---------------------------------
    // RANGE SELECTION
    // ---------------------------------

    pinMode(RANGE_PIN_1, INPUT_PULLUP);

    pinMode(RANGE_PIN_2, INPUT_PULLUP);

    selectSoundRange();


    // ---------------------------------
    // HC-SR04
    // ---------------------------------

    pinMode(TRIG_PIN, OUTPUT);

    pinMode(ECHO_PIN, INPUT);

    digitalWrite(TRIG_PIN, LOW);


    // ---------------------------------
    // LED STRIP
    // ---------------------------------

    FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>(
        leds,
        NUM_LEDS
    );

    FastLED.setBrightness(LED_BRIGHTNESS);

    FastLED.clear();

    FastLED.show();


    // ---------------------------------
    // LED TEST
    // ---------------------------------

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


    // ---------------------------------
    // I2S CONFIGURATION
    // ---------------------------------

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


    // ---------------------------------
    // STARTUP INFORMATION
    // ---------------------------------

    Serial.println();

    Serial.println("==============================");

    Serial.println(" SOUND LEVEL DETECTOR");

    Serial.println(" INMP441 + WS2812B + HC-SR04");

    Serial.println("==============================");

    Serial.println("I2S started");

    Serial.println("LED strip initialized");

    Serial.println("HC-SR04 started");

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

    Serial.println("Object detection distance: 20 cm");

    Serial.println("Result display time: 10 seconds");

    Serial.println("System ready");


    // ---------------------------------
    // START BREATHING
    // ---------------------------------

    soundAnalysisEnabled = false;

    waitForObjectClear = false;

    resetSoundEvent();

    startBreathing();

}


// =====================================
// MAIN LOOP
// =====================================

void loop() {

    // =================================
    // 1. READ MICROPHONE CONTINUOUSLY
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
    // 2. CALCULATE RMS CONTINUOUSLY
    // =================================

    int32_t rms = calculateRMS(frames);


    // =================================
    // 3. UPDATE ULTRASONIC SENSOR
    // =================================

    bool previousObjectPresent = objectPresent;

    updateObjectDetection();


    // =================================
    // 4. OBJECT ENTERS DETECTION ZONE
    // =================================

    if (!previousObjectPresent && objectPresent) {

        if (
            currentLedMode == LED_BREATHING &&
            !waitForObjectClear
        ) {

              // Включаем зелёную ленту
            showObjectDetected();

            soundAnalysisEnabled = true;

            resetSoundEvent();

            Serial.println();
            Serial.println("================================");
            Serial.println("SOUND ANALYSIS ENABLED");
            Serial.println("================================");

        }

    }


    // =================================
    // 5. OBJECT LEAVES DETECTION ZONE
    // =================================

    if (previousObjectPresent && !objectPresent) {

        waitForObjectClear = false;

        if (currentLedMode != LED_RESULT) {

            soundAnalysisEnabled = false;

            resetSoundEvent();

            Serial.println("SOUND ANALYSIS DISABLED");

            startBreathing();

        }

    }


    // =================================
    // 6. RESULT DISPLAY HAS PRIORITY
    // =================================

    if (currentLedMode == LED_RESULT) {

        if (
            millis() - resultStartTime >= RESULT_TIME_MS
        ) {

            finishResult();

        }

        return;

    }


    // =================================
    // 7. UPDATE SOUND RANGE
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
    // 8. PROCESS SOUND ONLY WHEN
    //    OBJECT IS DETECTED
    // =================================

    if (soundAnalysisEnabled) {

        processSoundBlock(rms);

    }


    // =================================
    // 9. BREATHING ANIMATION
    // =================================

    if (currentLedMode == LED_BREATHING) {

        updateBreathing();

    }


    // =================================
    // 10. SERIAL OUTPUT
    // =================================

    Serial.printf(
        ">RMS:%ld\n",
        (long)rms
    );

}
