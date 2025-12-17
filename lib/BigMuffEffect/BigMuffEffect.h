#pragma once
#include <Arduino.h>
#include <stdint.h>

class BigMuffEffect {
public:
  // **************************
  // Params
  // *****************
  struct Params {
    float drive = 0.0f; // sustain
    float tone  = 0.0f; // tone
    float shape = 0.0f; // clip hardness
    float pres  = 0.0f; // presence / fizz control
  };

  BigMuffEffect();
  void reset();

  // in-place mono (wet only)
  void processMonoWet(int16_t* mono, int n, float fs, const Params& p);

private:
  // **************************
  // State
  // *****************

  // DC blocker
  float _dc_x1 = 0.0f;
  float _dc_y1 = 0.0f;

  // coupling HP (via LP state)
  float _hp1_lp = 0.0f;
  float _hp2_lp = 0.0f;
  float _hp3_lp = 0.0f;

  // LP states
  float _preLP  = 0.0f; // pre-clip bandlimit
  float _postLP = 0.0f; // post-clip fizz kill
  float _toneLP = 0.0f; // tone LP

  // oversample AA
  float _osLP   = 0.0f;

  // 2x interp helper
  float _xPrev  = 0.0f;

  // **************************
  // Helpers
  // *****************
  static float clamp01(float x);
  static float lerp(float a, float b, float t);
  static int16_t clamp16(int32_t x);

  static float dcBlock(float x, float& x1, float& y1, float R);
  static float onePoleLP(float x, float& y, float a);
  static float onePoleHP_viaLP(float x, float& lpState, float a);

  static float satAtan(float x, float k); // smooth sat
  static float softLimit(float x);        // safety
};
