// Console BRAIN board — runs the OS + all 10 games, reads the two rotary knobs,
// drives the speaker + the 160x80 TFT dashboard, and streams the composed
// Color[108] frames over the COBS serial link to the screen board. Pins match
// the Atech 14-port controller build.
#include <Arduino.h>
#include "esp_system.h"  // esp_random() — a real per-boot RNG seed

#include "console_brain.h"
#include "rotary_encoder.h"
#include "speaker.h"
#include "st7735_tft.h"

// Brain board pins (from the generated controller build).
static RotaryEncoder knob0(5, 4, 9, 8);    // knob_p1: clk,dt,sw,ring  (ports 1,2)
static RotaryEncoder knob1(40, 41, 1, 2);  // knob_p2: clk,dt,sw,ring  (ports 9,10)
static Speaker       speaker(15, 16, 18);  // I2S bclk,lrc,dout        (ports 3,4)
static ST7735_TFT    tft(35, 38, 36, 39);  // sclk,cs,mosi,dc          (ports 5,6)
static ConsoleBrain  brain(knob0, knob1, speaker, tft);

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);  // stream frames without blocking if unread

    knob0.begin();
    knob1.begin();
    speaker.begin();
    speaker.setVolume(0.15f);         // MAX98357A clips near 1.0 — quiet by default
    knob0.setAcceleration(false, 1);  // 1 detent/step — menus want precision
    knob1.setAcceleration(false, 1);

    brain.begin(esp_random());
}

void loop() {
    knob0.update();
    knob1.update();
    brain.service(millis());
}
