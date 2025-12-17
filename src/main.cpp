// main.cpp
// Teensy 4.0 DSP Pedal (mono output on LEFT only)
// Tap button to cycle effects
// Pots are wired backwards, so we flip them in code
// RGB LED shows which mode we’re in

#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>

#include "LeslieEffect.h"
#include "BigMuffEffect.h"
#include "OctaveEffect.h"
#include "OrchestraEffect.h"
#include "SimpleEffects.h"  // bitcrush / flanger / trem + tiny filters

using namespace SimpleFX;

// ****************************
// Pin Definitions 
// ***************
static constexpr int POT1_PIN = A6; // pin 20
static constexpr int POT2_PIN = A3; // pin 17
static constexpr int POT3_PIN = A2; // pin 16
static constexpr int POT4_PIN = A1; // pin 15
static constexpr int POT5_PIN = A0; // pin 14

static constexpr int BTN_PIN  = 2;

// RGB LED pins
static constexpr int LED_R = 3;
static constexpr int LED_G = 4;
static constexpr int LED_B = 5;

// ***********************
// Small Helpers
// *****************
static inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

// Sets LED color
static inline void setLED(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(LED_R, r);
  analogWrite(LED_G, g);
  analogWrite(LED_B, b);
}

// Flips pot turning directions
static inline float readPot01Flipped(int pin) {
  float v = (float)analogRead(pin) / 1023.0f;
  v = clamp01(v);
  return 1.0f - v;
}

// ***************************
// Modes
// *******

enum Mode : uint8_t {
  MODE_BYPASS = 0,
  MODE_LESILE = 1,
  MODE_MUFF   = 2,
  MODE_OCTAVE = 3,
  MODE_ORCH   = 4,

  MODE_CRUSH  = 5,
  MODE_FLANGE = 6,
  MODE_TREM   = 7,
  MODE_CHORUS = 8,
};

static Mode mode = MODE_BYPASS;
static constexpr uint8_t MODE_COUNT = 9;

// ******************************
// Effect Objects
// ****************
static LeslieEffect    leslie;
static BigMuffEffect   muff;
static OctaveEffect    octave;
static OrchestraEffect orchestra;

// Simple FX from SimpleEffects.h
static BitCrusher  crush;
static Flanger     flanger;
static Tremolo     trem;

// Chorus is basically “flanger but longer delay + no feedback”
static Flanger     chorus;

// Tiny filters for quick cleanup
static OnePoleHPF  hpf;
static OnePoleLPF  lpf;

// **************************
// Pot Smoothing
// ****************
static float s1=0, s2=0, s3=0, s4=0, s5=0;
static constexpr float POT_ALPHA = 0.15f;


static void readControls(float& param1, float& param2, float& param3, float& param4, float& param5) {
  float r1 = readPot01Flipped(POT5_PIN); // param1 (volume)
  float r2 = readPot01Flipped(POT4_PIN); // param2
  float r3 = readPot01Flipped(POT3_PIN); // param3
  float r4 = readPot01Flipped(POT2_PIN); // param4
  float r5 = readPot01Flipped(POT1_PIN); // param5

  // low-pass smoothing
  s1 += POT_ALPHA * (r1 - s1);
  s2 += POT_ALPHA * (r2 - s2);
  s3 += POT_ALPHA * (r3 - s3);
  s4 += POT_ALPHA * (r4 - s4);
  s5 += POT_ALPHA * (r5 - s5);

  param1 = s1; param2 = s2; param3 = s3; param4 = s4; param5 = s5;
}

// *********************************
// Button Debounce
// ********************
static bool lastBtn = true;
static uint32_t lastEdgeMs = 0;

// LED color for each mode
static void applyModeLED() {
  switch (mode) {
    case MODE_BYPASS: setLED(0,   0,   0);   break; // off
    case MODE_LESILE: setLED(255, 255, 0);   break; // yellow
    case MODE_MUFF:   setLED(255, 0,   0);   break; // red
    case MODE_OCTAVE: setLED(0,   255, 0);   break; // green
    case MODE_ORCH:   setLED(255, 90,  0);   break; // orange

    case MODE_CRUSH:  setLED(180, 0,   255); break; // purple
    case MODE_FLANGE: setLED(0,   180, 255); break; // cyan
    case MODE_TREM:   setLED(0,   0,   255); break; // blue
    case MODE_CHORUS: setLED(255, 255, 255); break; // white
  }
}

// Reset effect state to prevent switching artifacts
static void resetAllStates() {
  leslie.reset();
  muff.reset();
  octave.reset();
  orchestra.reset();

  crush.reset();
  flanger.reset();
  trem.reset();
  chorus.reset();

  hpf.reset();
  lpf.reset();
}

static void cycleMode() {
  mode = (Mode)((((uint8_t)mode) + 1) % MODE_COUNT);
  applyModeLED();
  resetAllStates();
}

static void updateButton() {
  bool pressed = (digitalRead(BTN_PIN) == LOW);
  uint32_t now = millis();

  // 40ms debounce
  if (pressed != lastBtn && (now - lastEdgeMs) > 40) {
    lastEdgeMs = now;
    lastBtn = pressed;
    if (pressed) cycleMode();
  }
}

// ********************************
// Custom Audio Stream
// **************************
class FxStream : public AudioStream {
public:
  FxStream() : AudioStream(2, queue) {}

  void update() override {
    audio_block_t* L = receiveWritable(0);
    audio_block_t* R = receiveWritable(1);

    if (!L || !R) {
      if (L) release(L);
      if (R) release(R);
      return;
    }

    float vol, k2, k3, k4, k5;
    readControls(vol, k2, k3, k4, k5);

    constexpr float FS = AUDIO_SAMPLE_RATE_EXACT;

    // Treat input as mono
    int16_t inMono[AUDIO_BLOCK_SAMPLES];
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t m = (int32_t)L->data[i] + (int32_t)R->data[i];
      inMono[i] = (int16_t)(m / 2);
    }

    // Start with dry signal, then overwrite depending on mode
    int16_t outMono[AUDIO_BLOCK_SAMPLES];
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) outMono[i] = inMono[i];

    // ********************************************************************
    // Mode Processing
    // ********************

    // BYPASS, let clean audio thru
    if (mode == MODE_BYPASS) {
    
    }

    // LESLIE: effect runs stereo internally, then collapse to mono after mixing
    else if (mode == MODE_LESILE) {
      // K2 blend, K3 speed, K4 depth, K5 ramp
      LeslieEffect::Params lp;
      lp.volume = 1.0f; // keep per-effect volume consistent, do final volume at the end
      lp.blend  = k2;
      lp.speed  = k3;
      lp.depth  = k4;
      lp.ramp   = k5;

      for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        L->data[i] = inMono[i];
        R->data[i] = inMono[i];
      }

      leslie.processWet(L->data, R->data, AUDIO_BLOCK_SAMPLES, FS, lp);

      // Mix back to mono with blend control
      for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        float wetMono = 0.5f * ((float)L->data[i] + (float)R->data[i]);
        float dryM    = (float)inMono[i];
        float mix     = (1.0f - lp.blend) * dryM + lp.blend * wetMono;

        int32_t y = (int32_t)mix;
        if (y > 32767) y = 32767;
        else if (y < -32768) y = -32768;
        outMono[i] = (int16_t)y;
      }
    }

    // BIG MUFF
    else if (mode == MODE_MUFF) {
      // K2 tone, K3 drive, K4 shape, K5 presence
      BigMuffEffect::Params mp;
      mp.tone  = k2;
      mp.drive = k3;
      mp.shape = k4;
      mp.pres  = k5;

      muff.processMonoWet(outMono, AUDIO_BLOCK_SAMPLES, FS, mp);
    }

    // OCTAVE
    else if (mode == MODE_OCTAVE) {
      // K2 blend, K3 octave mix, K4 tracking, K5 character
      OctaveEffect::Params op;
      op.blend     = k2;
      op.mix       = k3;
      op.tracking  = k4;
      op.character = k5;

      octave.processMono(inMono, outMono, AUDIO_BLOCK_SAMPLES, FS, op);
    }

    // ORCHESTRA
    else if (mode == MODE_ORCH) {
      // K2 blend, K3 size, K4 shimmer, K5 swell
      OrchestraEffect::Params op;
      op.mix   = k2;
      op.size  = k3;
      op.up    = k4;
      op.down  = 0.75f * k4;
      op.swell = k5;
      op.tone  = 0.55f; // fixed darker so it isn’t painfully bright

      orchestra.processMono(inMono, outMono, AUDIO_BLOCK_SAMPLES, FS, op);
    }

    // BITCRUSH: reduce bits + sample rate
    else if (mode == MODE_CRUSH) {
      // K2 blend, K3 bit depth, K4 SR reduce, K5 edge
      float mix = k2;
      int bits  = 1 + (int)(k3 * 15.0f);   // 1..16
      int down  = 1 + (int)(k4 * 31.0f);   // 1..32

      crush.setSampleRate(FS);
      crush.setParams(bits, down, mix);
      crush.processBlock(outMono, AUDIO_BLOCK_SAMPLES);

      // Edge knob = light filtering so it’s crunchy but not pure sand
      if (k5 > 0.02f) {
        hpf.setSampleRate(FS);
        hpf.setCutoffHz(20.0f + 140.0f * k5);
        hpf.processBlock(outMono, AUDIO_BLOCK_SAMPLES);

        lpf.setSampleRate(FS);
        lpf.setCutoffHz(2500.0f + 9000.0f * (1.0f - k5));
        lpf.processBlock(outMono, AUDIO_BLOCK_SAMPLES);
      }
    }

    // FLANGER
    else if (mode == MODE_FLANGE) {
      // K2 blend, K3 rate, K4 depth, K5 feedback
      float mix     = k2;
      float rateHz  = 0.05f + 4.0f * k3;
      float depthMs = 0.2f  + 6.0f * k4;
      float fb      = -0.8f + 1.6f * k5;
      float baseMs  = 0.7f + 2.5f * (1.0f - k4);

      flanger.setSampleRate(FS);
      flanger.setParams(baseMs, depthMs, rateHz, fb, mix);
      flanger.processBlock(outMono, AUDIO_BLOCK_SAMPLES);

      // Quick LPF so it doesn’t get too “metallic”
      lpf.setSampleRate(FS);
      lpf.setCutoffHz(3500.0f + 8000.0f * (1.0f - k4));
      lpf.processBlock(outMono, AUDIO_BLOCK_SAMPLES);
    }

    // TREMOLO
    else if (mode == MODE_TREM) {
      // K2 blend, K3 rate, K4 depth, K5 chop
      float mix    = k2;
      float rateHz = 0.2f + 12.0f * k3;
      float depth  = k4;

      trem.setSampleRate(FS);
      trem.setParams(rateHz, depth, mix);
      trem.processBlock(outMono, AUDIO_BLOCK_SAMPLES);

      // Chop = a little HPF to make it feel sharper
      if (k5 > 0.02f) {
        hpf.setSampleRate(FS);
        hpf.setCutoffHz(15.0f + 120.0f * k5);
        hpf.processBlock(outMono, AUDIO_BLOCK_SAMPLES);
      }
    }

    // CHORUS (default else)
    else {
      // K2 blend, K3 rate, K4 depth, K5 tone
      float mix     = k2;
      float rateHz  = 0.08f + 2.5f * k3;       // chorus likes slower
      float depthMs = 1.0f  + 10.0f * k4;      // longer mod delay than flanger
      float baseMs  = 10.0f + 6.0f * (1.0f - k4);

      chorus.setSampleRate(FS);
      chorus.setParams(baseMs, depthMs, rateHz, 0.0f, mix); // no feedback for chorus
      chorus.processBlock(outMono, AUDIO_BLOCK_SAMPLES);

      // Tone knob is lpf
      lpf.setSampleRate(FS);
      lpf.setCutoffHz(1200.0f + 12000.0f * k5);
      lpf.processBlock(outMono, AUDIO_BLOCK_SAMPLES);

      // Small HPF so the low end doesnt get muddy
      hpf.setSampleRate(FS);
      hpf.setCutoffHz(15.0f);
      hpf.processBlock(outMono, AUDIO_BLOCK_SAMPLES);
    }

    // **************************
    // Final Output Stuff
    // **************************

    // Global volume applied at the end
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t y = (int32_t)((float)outMono[i] * vol);
      if (y > 32767) y = 32767;
      else if (y < -32768) y = -32768;
      outMono[i] = (int16_t)y;
    }

    // Output is mono on LEFT only, right channel muted
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      L->data[i] = outMono[i];
      R->data[i] = 0;
    }

    transmit(L, 0);
    transmit(R, 1);
    release(L);
    release(R);
  }

private:
  audio_block_t* queue[2];
};

// **************************
// Audio Graph Wiring
// ********************
AudioInputI2S        i2sIn;
FxStream             fx;
AudioOutputI2S       i2sOut;

AudioConnection      patchL(i2sIn, 0, fx, 0);
AudioConnection      patchR(i2sIn, 1, fx, 1);
AudioConnection      outL(fx, 0, i2sOut, 0);
AudioConnection      outR(fx, 1, i2sOut, 1);

AudioControlSGTL5000 sgtl5000;

// **************************
// Setup / Loop
// **************************
void setup() {
  pinMode(BTN_PIN, INPUT_PULLUP);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  setLED(0, 0, 0);
  applyModeLED();

  analogReadResolution(10);

  // Audio memory blocks
  AudioMemory(90);

  // Audio shield init
  sgtl5000.enable();
  sgtl5000.volume(0.5f);
  sgtl5000.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000.lineInLevel(0);

  // Pre-set sample rates
  crush.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
  flanger.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
  trem.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
  chorus.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
  hpf.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);
  lpf.setSampleRate(AUDIO_SAMPLE_RATE_EXACT);

  resetAllStates();
}

void loop() {
  updateButton();
  delay(2); // tiny delay so loop isn’t running at max speed for no reason
}

/*
KNOB CONSISTENCY:
- VOL = output level (always)
- K2  = Dry/Wet (if the effect has it)
- K3  = Speed/Rate/Drive
- K4  = Depth/Amount
- K5  = Character/Tone/Feedback

MODE COLORS:
- BYPASS  : off
- LESLIE  : yellow
- MUFF    : red
- OCTAVE  : green
- ORCH    : orange
- CRUSH   : purple
- FLANGE  : cyan
- TREM    : blue
- CHORUS  : white
*/
