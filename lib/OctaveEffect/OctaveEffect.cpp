#include "OctaveEffect.h"
#include <math.h>

OctaveEffect::OctaveEffect() { reset(); }

void OctaveEffect::reset() {
  _env = 0.0f;
  _samplesSinceCross = 0;
  _freqSmoothed = 200.0f;
  _phase = 0.0f;
  _sign = 0;

  _trackingActive = false;
  _hangSamples = 0;

  _preLP  = 0.0f;
  _upLP   = 0.0f;
  _upDC   = 0.0f;
  _oscLP  = 0.0f;
  _postLP = 0.0f;
}

float OctaveEffect::clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

int16_t OctaveEffect::clamp16(int32_t x) {
  if (x > 32767) return 32767;
  if (x < -32768) return -32768;
  return (int16_t)x;
}

float OctaveEffect::lerp(float a, float b, float t) { return a + (b - a) * t; }

// one pole LP
float OctaveEffect::onePoleLP(float x, float& y, float a) {
  y = y + a * (x - y);
  return y;
}

// atan sat (cheap + smooth)
float OctaveEffect::satAtan(float x, float k) {
  return (2.0f / 3.14159265f) * atanf(k * x);
}

void OctaveEffect::processMono(const int16_t* monoIn, int16_t* monoOut, int n, float fs, const Params& pIn) {
  float blend     = clamp01(pIn.blend);
  float mix       = clamp01(pIn.mix);
  float tracking  = clamp01(pIn.tracking);
  float character = clamp01(pIn.character);

  // **************************
  // up/down weights (mix)
  // *****************
  float wDown, wUp;
  if (mix <= 0.5f) {
    wDown = 1.0f;
    wUp   = 2.0f * mix;
  } else {
    wDown = 2.0f * (1.0f - mix);
    wUp   = 1.0f;
  }
  float wScale = 0.5f * (wDown + wUp);
  if (wScale < 0.5f) wScale = 0.5f;

  // **************************
  // tracker tuning
  // *****************
  float gateBase = lerp(0.030f, 0.006f, tracking);  // tracking up => gate down
  float gateOn   = gateBase * 1.25f;                // hysteresis
  float gateOff  = gateBase * 0.75f;

  int   hangMax     = (int)(fs * lerp(0.15f, 0.20f, tracking)); // hold lock a bit
  float freqSmooth  = lerp(0.03f, 0.22f, tracking);             // tracking up => faster updates
  float maxJump     = lerp(0.18f, 0.55f, tracking);             // limit pitch teleport
  float hyst        = lerp(0.004f, 0.012f, tracking);           // crossing hysteresis

  // prefilter for crossings (helps noisy inputs)
  float preHz = 900.0f;
  float preA = preHz / fs;
  if (preA < 0.001f) preA = 0.001f;
  if (preA > 0.45f)  preA = 0.45f;

  // **************************
  // octave up tuning
  // *****************
  float upHz = lerp(2500.0f, 7500.0f, character);
  float upA = upHz / fs;
  if (upA < 0.001f) upA = 0.001f;
  if (upA > 0.45f)  upA = 0.45f;

  float upDCHz = 40.0f;
  float upDCA = upDCHz / fs;
  if (upDCA < 0.0005f) upDCA = 0.0005f;

  float upDrive = lerp(2.0f, 7.0f, character);
  float upGain  = lerp(2.8f, 4.2f, character);   // LOUDER: explicit up gain

  // **************************
  // octave down osc character
  // *****************
  float sqMix = character; // 0=sine, 1=square-ish

  float oscLPHz = 5000.0f;
  float oscLPA = oscLPHz / fs;
  if (oscLPA < 0.001f) oscLPA = 0.001f;
  if (oscLPA > 0.45f)  oscLPA = 0.45f;

  float postHz = lerp(1800.0f, 5200.0f, character);
  float postA = postHz / fs;
  if (postA < 0.001f) postA = 0.001f;
  if (postA > 0.45f)  postA = 0.45f;

  const float fMin = 55.0f;
  const float fMax = 800.0f;

  for (int i = 0; i < n; i++) {
    float x = (float)monoIn[i] / 32768.0f;

    // env follower
    float ax = fabsf(x);
    float envA = 0.015f;
    float envR = 0.0030f;
    _env = _env + ((ax > _env) ? envA : envR) * (ax - _env);

    // gate + hang (keep lock during decay)
    if (!_trackingActive) {
      if (_env > gateOn) {
        _trackingActive = true;
        _hangSamples = hangMax;
      }
    } else {
      if (_env < gateOff) {
        if (_hangSamples > 0) _hangSamples--;
        else _trackingActive = false;
      } else {
        _hangSamples = hangMax;
      }
    }

    // **************************
    // OCTAVE UP
    // *****************
    float up = fabsf(x);                 // rectifier
    up = onePoleLP(up, _upLP, upA);      // smooth it
    up = satAtan(up * upDrive, 2.5f);    // add some bite

    float dc = onePoleLP(up, _upDC, upDCA);
    up = (up - dc) * upGain;             // LOUDER

    // **************************
    // OCTAVE DOWN (mono tracker)
    // *****************
    float down = 0.0f;

    float xt = onePoleLP(x, _preLP, preA);
    _samplesSinceCross++;

    int newSign = _sign;
    if (xt >  hyst) newSign = +1;
    if (xt < -hyst) newSign = -1;

    bool crossing = (newSign > 0 && _sign <= 0);
    _sign = newSign;

    if (crossing) {
      int period = _samplesSinceCross;
      _samplesSinceCross = 0;

      if (_trackingActive && period > 10) {
        float f = fs / (float)period;

        if (f >= fMin && f <= fMax) {
          float fCand = f;

          // octave error snap (common on zero-cross trackers)
          if (fCand > 1.80f * _freqSmoothed && fCand < 2.20f * _freqSmoothed) fCand *= 0.5f;
          if (fCand > 0.45f * _freqSmoothed && fCand < 0.55f * _freqSmoothed) fCand *= 2.0f;

          // limit jump per update
          float lo = _freqSmoothed * (1.0f - maxJump);
          float hi = _freqSmoothed * (1.0f + maxJump);
          if (fCand < lo) fCand = lo;
          if (fCand > hi) fCand = hi;

          _freqSmoothed = _freqSmoothed + freqSmooth * (fCand - _freqSmoothed);
        }
      }
    }

    // no lock => drift to something safe
    if (!_trackingActive) {
      _freqSmoothed = _freqSmoothed + 0.01f * (120.0f - _freqSmoothed);
    }

    float fDown = 0.5f * _freqSmoothed;
    if (fDown < 20.0f) fDown = 20.0f;

    _phase += fDown / fs;
    if (_phase >= 1.0f) _phase -= 1.0f;

    // sine -> optional square blend
    float s = sinf(2.0f * 3.14159265f * _phase);
    float q = (s >= 0.0f) ? 1.0f : -1.0f;
    float osc = (1.0f - sqMix) * s + sqMix * q;

    osc = onePoleLP(osc, _oscLP, oscLPA);

    // fade down osc with env so it stops hanging
    float envGate = (_env - gateOff) * 30.0f;
    if (envGate < 0.0f) envGate = 0.0f;
    if (envGate > 1.0f) envGate = 1.0f;

    if (!_trackingActive) envGate *= 0.25f; // kill faster when not locked
    down = osc * envGate;

    // **************************
    // combine + blend
    // *****************
    float wet = (wDown * down + wUp * up) / wScale;
    wet = onePoleLP(wet, _postLP, postA);

    float y = (1.0f - blend) * x + blend * wet;

    int32_t out = (int32_t)(y * 32767.0f);
    monoOut[i] = clamp16(out);
  }
}
