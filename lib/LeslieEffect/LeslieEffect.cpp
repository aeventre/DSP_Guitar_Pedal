#include "LeslieEffect.h"
#include <math.h>

LeslieEffect::LeslieEffect() {
  reset();
}

void LeslieEffect::reset() {
  _lowL = 0.0f;
  _lowR = 0.0f;

  _phHorn = 0.0f;
  _phDrum = 0.0f;

  _hornHz = 0.8f;
  _drumHz = 0.6f;

  _idxHorn = 0;
  _idxDrum = 0;

  // wipe delay buffers
  for (int i = 0; i < BUF_LEN; i++) {
    _hornBufL[i] = 0;
    _hornBufR[i] = 0;
    _drumBufL[i] = 0;
    _drumBufR[i] = 0;
  }
}

float LeslieEffect::clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

int16_t LeslieEffect::clamp16(int32_t x) {
  if (x > 32767) return 32767;
  if (x < -32768) return -32768;
  return (int16_t)x;
}

float LeslieEffect::lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

float LeslieEffect::wrap01(float x) {
  if (x >= 1.0f) x -= 1.0f;
  if (x < 0.0f)  x += 1.0f;
  return x;
}

float LeslieEffect::cosine01(float phase01) {
  float c = cosf(2.0f * 3.14159265f * phase01);
  return 0.5f + 0.5f * c;
}

// fractional delay read (linear interp)
float LeslieEffect::fracDelayRead(const int16_t* buf, int bufLen, int writeIdx, float delaySamps) {
  float rp = (float)writeIdx - delaySamps;
  while (rp < 0.0f) rp += (float)bufLen;
  while (rp >= (float)bufLen) rp -= (float)bufLen;

  int i0 = (int)rp;
  int i1 = i0 + 1;
  if (i1 >= bufLen) i1 = 0;

  float t = rp - (float)i0;
  return (1.0f - t) * (float)buf[i0] + t * (float)buf[i1];
}

void LeslieEffect::processWet(int16_t* left, int16_t* right, int n, float fs, const Params& pIn) {
  // **************************
  // params
  // *****************
  float speed = clamp01(pIn.speed);
  float depth = clamp01(pIn.depth);
  float ramp  = clamp01(pIn.ramp);   // keep edges valid (no remap)

  // **************************
  // speed ranges
  // *****************
  const float hornSlow = 0.8f, hornFast = 6.0f;
  const float drumSlow = 0.6f, drumFast = 4.5f;

  float hornTarget = lerp(hornSlow, hornFast, speed);
  float drumTarget = lerp(drumSlow, drumFast, speed);

  // inertia (update once per block)
  float dt = (float)n / fs;

  // tau grows with ramp
  float hornTau = lerp(0.20f, 1.20f, ramp);
  float drumTau = lerp(0.35f, 1.80f, ramp);

  // exp smoothing
  float hornA = 1.0f - expf(-dt / hornTau);
  float drumA = 1.0f - expf(-dt / drumTau);

  _hornHz += (hornTarget - _hornHz) * hornA;
  _drumHz += (drumTarget - _drumHz) * drumA;

  // **************************
  // crossover
  // *****************
  float x = 850.0f / fs; // ~850 Hz
  if (x < 0.001f) x = 0.001f;
  if (x > 0.45f)  x = 0.45f;
  float a = x;

  // **************************
  // doppler setup
  // *****************
  const float baseHorn = 140.0f;
  const float baseDrum = 200.0f;

  float dopHorn = lerp(3.0f, 26.0f, depth);
  float dopDrum = lerp(2.0f, 18.0f, depth);

  // directionality AM depth
  float amHorn = lerp(0.15f, 0.98f, depth);
  float amDrum = lerp(0.05f, 0.75f, depth);

  // stereo offsets
  const float micL = 0.00f;
  const float micR = 0.25f;

  float dPhHorn = _hornHz / fs;
  float dPhDrum = _drumHz / fs;

  for (int i = 0; i < n; i++) {
    float inL = (float)left[i];
    float inR = (float)right[i];

    // split low/high
    _lowL = _lowL + a * (inL - _lowL);
    _lowR = _lowR + a * (inR - _lowR);

    float lowL  = _lowL;
    float lowR  = _lowR;
    float highL = inL - lowL;
    float highR = inR - lowR;

    // write bands into buffers
    _hornBufL[_idxHorn] = clamp16((int32_t)highL);
    _hornBufR[_idxHorn] = clamp16((int32_t)highR);

    _drumBufL[_idxDrum] = clamp16((int32_t)lowL);
    _drumBufR[_idxDrum] = clamp16((int32_t)lowR);

    // update phases
    _phHorn = wrap01(_phHorn + dPhHorn);
    _phDrum = wrap01(_phDrum + dPhDrum);

    // doppler delay mod (cos)
    float hornModL = cosf(2.0f * 3.14159265f * (_phHorn + micL));
    float hornModR = cosf(2.0f * 3.14159265f * (_phHorn + micR));
    float drumModL = cosf(2.0f * 3.14159265f * (_phDrum + micL));
    float drumModR = cosf(2.0f * 3.14159265f * (_phDrum + micR));

    float dHornL = baseHorn + dopHorn * hornModL;
    float dHornR = baseHorn + dopHorn * hornModR;
    float dDrumL = baseDrum + dopDrum * drumModL;
    float dDrumR = baseDrum + dopDrum * drumModR;

    if (dHornL < 1.0f) dHornL = 1.0f;
    if (dHornR < 1.0f) dHornR = 1.0f;
    if (dDrumL < 1.0f) dDrumL = 1.0f;
    if (dDrumR < 1.0f) dDrumR = 1.0f;

    // read wet (frac delay)
    float hornWetL = fracDelayRead(_hornBufL, BUF_LEN, _idxHorn, dHornL);
    float hornWetR = fracDelayRead(_hornBufR, BUF_LEN, _idxHorn, dHornR);
    float drumWetL = fracDelayRead(_drumBufL, BUF_LEN, _idxDrum, dDrumL);
    float drumWetR = fracDelayRead(_drumBufR, BUF_LEN, _idxDrum, dDrumR);

    // AM gains
    float gHornL = (1.0f - amHorn) + amHorn * cosine01(_phHorn + micL);
    float gHornR = (1.0f - amHorn) + amHorn * cosine01(_phHorn + micR);
    float gDrumL = (1.0f - amDrum) + amDrum * cosine01(_phDrum + micL);
    float gDrumR = (1.0f - amDrum) + amDrum * cosine01(_phDrum + micR);

    // combine bands (keep headroom)
    float outL = (1.10f * hornWetL * gHornL + 0.90f * drumWetL * gDrumL) * 0.80f;
    float outR = (1.10f * hornWetR * gHornR + 0.90f * drumWetR * gDrumR) * 0.80f;

    left[i]  = clamp16((int32_t)outL);
    right[i] = clamp16((int32_t)outR);

    _idxHorn++;
    if (_idxHorn >= BUF_LEN) _idxHorn = 0;

    _idxDrum++;
    if (_idxDrum >= BUF_LEN) _idxDrum = 0;
  }
}
