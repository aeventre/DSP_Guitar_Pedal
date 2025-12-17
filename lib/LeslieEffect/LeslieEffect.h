#pragma once
#include <Arduino.h>
#include <stdint.h>

class LeslieEffect {
public:
  // **************************
  // Params
  // *****************
  struct Params {
    float volume = 0.0f; // main handles this
    float blend  = 0.0f; // main handles this
    float speed  = 0.0f; // slow -> fast
    float depth  = 0.0f; // how wide the wobble gets
    float ramp   = 0.0f; // snappy -> sluggish
  };

  LeslieEffect();
  void reset();

  // wet only (blend/volume happen in main)
  void processWet(int16_t* left, int16_t* right, int n, float fs, const Params& p);

private:
  // **************************
  // Crossover state
  // *****************
  float _lowL = 0.0f;
  float _lowR = 0.0f;

  // **************************
  // Phases (0..1)
  // *****************
  float _phHorn = 0.0f;
  float _phDrum = 0.0f;

  // **************************
  // Rotor speeds (Hz)
  // *****************
  float _hornHz = 0.8f;
  float _drumHz = 0.6f;

  // **************************
  // Delay buffers
  // *****************
  static constexpr int BUF_LEN = 4096;
  int16_t _hornBufL[BUF_LEN];
  int16_t _hornBufR[BUF_LEN];
  int16_t _drumBufL[BUF_LEN];
  int16_t _drumBufR[BUF_LEN];

  int _idxHorn = 0;
  int _idxDrum = 0;

  // **************************
  // Helpers
  // *****************
  static float clamp01(float x);
  static int16_t clamp16(int32_t x);
  static float lerp(float a, float b, float t);
  static float wrap01(float x);
  static float cosine01(float phase01);
  static float fracDelayRead(const int16_t* buf, int bufLen, int writeIdx, float delaySamps);
};
