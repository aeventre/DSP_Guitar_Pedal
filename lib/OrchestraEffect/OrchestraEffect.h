#pragma once
#include <stdint.h>

class OrchestraEffect {
public:
  // **************************
  // Params
  // *****************
  struct Params {
    float mix   = 0.75f; // wet/dry
    float size  = 0.85f; // sustain / feedback
    float swell = 0.70f; // duck + rise
    float up    = 0.80f; // +12 shimmer stage
    float down  = 0.65f; // -12 shimmer stage
    float tone  = 0.55f; // brighter -> darker (fb LPF)
  };

  OrchestraEffect();
  void reset();
  void processMono(const int16_t* inMono, int16_t* outMono, int n, float fs, const Params& p);

private:
  // **************************
  // Helpers
  // *****************
  static float clamp01(float x);
  static float lerp(float a, float b, float t);
  static int16_t clamp16(int32_t x);

  static float onePoleLP(float x, float& y, float a);
  static float onePoleHP_viaLP(float x, float& lpState, float a);
  static float softSat(float x, float drive);

  // **************************
  // Predelay (frac delay)
  // *****************
  struct DelayLine {
    static const int MAX = 8192;
    float buf[MAX];
    int w = 0;

    void reset();
    void push(float x);
    float readFrac(float dSamp) const;
  };

  // **************************
  // PitchShift (4 grain + Hann)
  // *****************
  struct PitchShift {
    static const int BUF = 8192;
    float buf[BUF];
    int w = 0;

    float ph[4];

    void reset();
    float process(float x, float ratio, float fs, float grainMs);
    float readFrac(float delaySamp) const;
    static float hann(float p01);
  };

  // **************************
  // Comb / Allpass
  // *****************
  struct Comb {
    static const int MAX = 4096;
    float buf[MAX];
    int len = 1, idx = 0;
    float lp = 0.0f;

    void init(int delay);
    void reset();
    float process(float x, float fb, float damp);
  };

  struct Allpass {
    static const int MAX = 2048;
    float buf[MAX];
    int len = 1, idx = 0;

    void init(int delay);
    void reset();
    float process(float x, float g);
  };

  // **************************
  // Shimmer Stage
  // *****************
  struct ShimmerStage {
    Comb c[4];
    Allpass ap[2];
    PitchShift ps;

    float fbLP  = 0.0f; // fb LPF state
    float wetLP = 0.0f; // wet smoothing
    float dcLP  = 0.0f; // DC cleanup state

    void init();
    void reset();
    float process(float x, float fs,
                  float size, float tone,
                  float shimmerAmt, float ratio,
                  float grainMs);
  };

  // **************************
  // State
  // *****************
  DelayLine _pre;

  // swell / ducking
  float _env     = 0.0f;
  float _duck    = 1.0f;
  float _swellLP = 0.0f;

  ShimmerStage _up;
  ShimmerStage _down;

  // output cleanup
  float _outLP = 0.0f;
  float _outDC = 0.0f;
};
