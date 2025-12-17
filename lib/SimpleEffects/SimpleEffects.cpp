#include "SimpleEffects.h"

namespace SimpleFX {

// ***********************
// BitCrusher
// ***************
void BitCrusher::processBlock(int16_t* data, int n) {
  if (!data || n <= 0) return;

  // quant levels from bit depth
  const int levels = 1 << _bits;
  const float invLevels = 1.0f / (float)levels;

  for (int i = 0; i < n; i++) {
    float x = int16ToFloat(data[i]);

    // sample/hold downsample
    if (_holdCount <= 0) {
      _held = x;
      _holdCount = _down;
    }
    _holdCount--;

    float crushed = _held;

    // quantize (float -> buckets -> float)
    float u = 0.5f * (crushed + 1.0f);
    int q = (int)(u * levels);
    q = (q < 0) ? 0 : (q > (levels - 1) ? (levels - 1) : q);
    float uq = (float)q * invLevels;
    crushed = 2.0f * uq - 1.0f;

    // dry/wet
    float y = (1.0f - _mix) * x + _mix * crushed;
    data[i] = floatToInt16(y);
  }
}

// ***********************
// Tremolo
// ***************
void Tremolo::processBlock(int16_t* data, int n) {
  if (!data || n <= 0) return;

  // phase step per sample
  const float phaseInc = (2.0f * (float)M_PI) * (_rate / _sr);

  for (int i = 0; i < n; i++) {
    float x = int16ToFloat(data[i]);

    // lfo 0..1
    float lfo = 0.5f * (sinf(_phase) + 1.0f);

    // gain (1-depth) .. 1
    float g = (1.0f - _depth) + _depth * lfo;

    float wet = x * g;
    float y = (1.0f - _mix) * x + _mix * wet;

    data[i] = floatToInt16(y);

    _phase += phaseInc;
    if (_phase > 2.0f * (float)M_PI) _phase -= 2.0f * (float)M_PI;
  }
}

// ************************
// Flanger
// ****************

// keep sr sane
void Flanger::setSampleRate(float sr) {
  _sr = (sr <= 8000.0f) ? 44100.0f : sr;
}

// wipe delay buffer + state
void Flanger::reset() {
  for (int i = 0; i < MAX_DELAY_SAMPLES; i++) _buf[i] = 0.0f;
  _w = 0;
  _phase = 0.0f;
}

void Flanger::processBlock(int16_t* data, int n) {
  if (!data || n <= 0) return;

  const float phaseInc = (2.0f * (float)M_PI) * (_rate / _sr);

  // ms -> samples
  const float msToSamples = _sr / 1000.0f;

  for (int i = 0; i < n; i++) {
    float x = int16ToFloat(data[i]);

    // delay mod from LFO
    float lfo = 0.5f * (sinf(_phase) + 1.0f);
    float delaySamp = (_baseMs + _depthMs * lfo) * msToSamples;

    // clamp so read stays in buffer
    if (delaySamp < 0.0f) delaySamp = 0.0f;
    if (delaySamp > (MAX_DELAY_SAMPLES - 2)) delaySamp = (float)(MAX_DELAY_SAMPLES - 2);

    // fractional read index
    float readIndex = (float)_w - delaySamp;
    while (readIndex < 0.0f) readIndex += (float)MAX_DELAY_SAMPLES;

    int idx0 = (int)readIndex;
    int idx1 = idx0 + 1;
    if (idx1 >= MAX_DELAY_SAMPLES) idx1 -= MAX_DELAY_SAMPLES;

    // linear interp
    float frac = readIndex - (float)idx0;
    float d0 = _buf[idx0];
    float d1 = _buf[idx1];
    float delayed = d0 + frac * (d1 - d0);

    // feedback write
    float writeVal = x + delayed * _fb;
    _buf[_w] = clampf(writeVal, -1.0f, 1.0f);

    _w++;
    if (_w >= MAX_DELAY_SAMPLES) _w = 0;

    // dry/wet
    float y = (1.0f - _mix) * x + _mix * delayed;
    data[i] = floatToInt16(y);

    _phase += phaseInc;
    if (_phase > 2.0f * (float)M_PI) _phase -= 2.0f * (float)M_PI;
  }
}

// ***********************
// SoftClip
// ***************
void SoftClip::processBlock(int16_t* data, int n) {
  if (!data || n <= 0) return;

  for (int i = 0; i < n; i++) {
    float x = int16ToFloat(data[i]);

    // drive first
    float xd = x * _drive;

    // soft clip
    float wet = xd / (1.0f + fabsf(xd));

    float y = (1.0f - _mix) * x + _mix * wet;
    data[i] = floatToInt16(y);
  }
}

// ************************
// OnePoleLPF
// ****************
void OnePoleLPF::setCutoffHz(float fc) {
  fc = clampf(fc, 5.0f, 0.45f * _sr);

  // one pole coeff
  float x = expf(-2.0f * (float)M_PI * fc / _sr);
  _a = x; // y[n] = (1-a)*x[n] + a*y[n-1]
}

void OnePoleLPF::processBlock(int16_t* data, int n) {
  if (!data || n <= 0) return;

  const float a = _a;
  const float b = 1.0f - a;

  float y = _y;
  for (int i = 0; i < n; i++) {
    float x = int16ToFloat(data[i]);
    y = b * x + a * y;
    data[i] = floatToInt16(y);
  }

  _y = y;
}

// ************************
// OnePoleHPF
// ****************
void OnePoleHPF::setCutoffHz(float fc) {
  fc = clampf(fc, 5.0f, 0.45f * _sr);

  // RC HPF coeff
  float rc = 1.0f / (2.0f * (float)M_PI * fc);
  float dt = 1.0f / _sr;
  _a = rc / (rc + dt);
}

void OnePoleHPF::processBlock(int16_t* data, int n) {
  if (!data || n <= 0) return;

  float a  = _a;
  float x1 = _x1;
  float y1 = _y1;

  for (int i = 0; i < n; i++) {
    float x = int16ToFloat(data[i]);
    float y = a * (y1 + x - x1);
    data[i] = floatToInt16(y);

    x1 = x;
    y1 = y;
  }

  _x1 = x1;
  _y1 = y1;
}

} // namespace SimpleFX
