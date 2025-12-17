#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <math.h>

namespace SimpleFX {

// **************************
// Helpers
// *****************

static inline float clampf(float x, float lo, float hi) {
  return (x < lo) ? lo : (x > hi) ? hi : x;
}

static inline float int16ToFloat(int16_t s) {
  // int16 -> float
  return (s >= 0) ? (s / 32767.0f) : (s / 32768.0f);
}

static inline int16_t floatToInt16(float x) {
  // clamp so it doesnt wrap
  x = clampf(x, -1.0f, 1.0f);

  // float -> int16
  return (x >= 0.0f) ? (int16_t)(x * 32767.0f) : (int16_t)(x * 32768.0f);
}

// **************************
// BitCrusher
// *****************
// bit depth + sample/hold downsample
class BitCrusher {
public:
  void setSampleRate(float sr) { _sr = sr; }

  // bits: 1..16
  // downsampleFactor: 1..128
  void setParams(int bits, int downsampleFactor, float mix = 1.0f) {
    _bits = (bits < 1) ? 1 : (bits > 16 ? 16 : bits);
    _down = (downsampleFactor < 1) ? 1 : (downsampleFactor > 128 ? 128 : downsampleFactor);
    _mix  = clampf(mix, 0.0f, 1.0f);
  }

  void reset() {
    _holdCount = 0;
    _held = 0.0f;
  }

  void processBlock(int16_t* data, int n);

private:
  float _sr = 44100.0f;

  int   _bits = 12;
  int   _down = 1;
  float _mix  = 1.0f;

  int   _holdCount = 0;
  float _held = 0.0f;
};

// **************************
// Tremolo
// *****************
// sine LFO amp mod
class Tremolo {
public:
  void setSampleRate(float sr) { _sr = sr; }

  // rateHz: speed
  // depth: 0..1
  // mix: 0..1
  void setParams(float rateHz, float depth, float mix = 1.0f) {
    _rate  = clampf(rateHz, 0.01f, 30.0f);
    _depth = clampf(depth, 0.0f, 1.0f);
    _mix   = clampf(mix, 0.0f, 1.0f);
  }

  void reset() { _phase = 0.0f; }

  void processBlock(int16_t* data, int n);

private:
  float _sr = 44100.0f;

  float _rate  = 4.0f;
  float _depth = 0.6f;
  float _mix   = 1.0f;

  float _phase = 0.0f; // radians
};

// **************************
// Flanger
// *****************
// short mod delay + feedback
class Flanger {
public:
  void setSampleRate(float sr);

  // baseDelayMs: base delay
  // depthMs: LFO swing
  // rateHz: LFO speed
  // feedback: -0.95..0.95
  // mix: 0..1
  void setParams(float baseDelayMs, float depthMs, float rateHz,
                 float feedback = 0.2f, float mix = 0.6f) {
    _baseMs  = clampf(baseDelayMs, 0.0f, 15.0f);
    _depthMs = clampf(depthMs,    0.0f, 15.0f);
    _rate    = clampf(rateHz,     0.01f, 10.0f);
    _fb      = clampf(feedback,  -0.95f, 0.95f);
    _mix     = clampf(mix,        0.0f, 1.0f);
  }

  void reset();
  void processBlock(int16_t* data, int n);

private:
  // keep this small, flanger only needs a few ms
  static constexpr int MAX_DELAY_SAMPLES = 2048; // ~46ms at 44.1k
  float _buf[MAX_DELAY_SAMPLES] = {0};

  float _sr = 44100.0f;
  int   _w  = 0;

  float _baseMs  = 2.0f;
  float _depthMs = 1.5f;
  float _rate    = 0.25f;
  float _fb      = 0.2f;
  float _mix     = 0.6f;

  float _phase = 0.0f; // radians
};

// **************************
// SoftClip
// *****************
// gentle saturation / safety
class SoftClip {
public:
  void setParams(float drive = 1.5f, float mix = 1.0f) {
    _drive = clampf(drive, 0.1f, 20.0f);
    _mix   = clampf(mix,   0.0f, 1.0f);
  }

  void processBlock(int16_t* data, int n);

private:
  float _drive = 1.5f;
  float _mix   = 1.0f;
};

// **************************
// One-Pole Filters
// *****************
// basic tone shaping
class OnePoleLPF {
public:
  void setSampleRate(float sr) { _sr = sr; }
  void setCutoffHz(float fc);

  void reset(float y = 0.0f) { _y = y; }

  void processBlock(int16_t* data, int n);

private:
  float _sr = 44100.0f;
  float _a  = 0.0f;
  float _y  = 0.0f;
};

class OnePoleHPF {
public:
  void setSampleRate(float sr) { _sr = sr; }
  void setCutoffHz(float fc);

  void reset(float x = 0.0f, float y = 0.0f) { _x1 = x; _y1 = y; }

  void processBlock(int16_t* data, int n);

private:
  float _sr = 44100.0f;
  float _a  = 0.0f;
  float _x1 = 0.0f;
  float _y1 = 0.0f;
};

} // namespace SimpleFX
