#pragma once
#include <Arduino.h>
#include <stdint.h>

class OctaveEffect {
public:
  // **************************
  // Params
  // *****************
  struct Params {
    float blend     = 0.0f; // dry/wet
    float mix       = 0.0f; // 0=down, 0.5=both, 1=up
    float tracking  = 0.0f; // stability (gate/hang/smoothing)
    float character = 0.0f; // wave shape + up drive
  };

  OctaveEffect();
  void reset();

  void processMono(const int16_t* monoIn, int16_t* monoOut, int n, float fs, const Params& p);

private:
  // **************************
  // Env + tracking state
  // *****************
  float _env = 0.0f;
  int   _samplesSinceCross = 0;
  float _freqSmoothed = 200.0f; // Hz
  float _phase = 0.0f;          // 0..1
  int   _sign = 0;

  // gate hysteresis + hang
  bool _trackingActive = false;
  int  _hangSamples = 0;

  // **************************
  // Filter states
  // *****************
  float _preLP   = 0.0f; // tracker prefilter
  float _upLP    = 0.0f; // octave-up smoothing
  float _upDC    = 0.0f; // octave-up DC estimate
  float _oscLP   = 0.0f; // smooth osc edges
  float _postLP  = 0.0f; // final smoothing

  // **************************
  // Helpers
  // *****************
  static float clamp01(float x);
  static int16_t clamp16(int32_t x);
  static float lerp(float a, float b, float t);

  static float onePoleLP(float x, float& y, float a);
  static float satAtan(float x, float k);
};
