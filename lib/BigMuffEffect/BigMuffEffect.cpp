#include "BigMuffEffect.h"
#include <math.h>

BigMuffEffect::BigMuffEffect() { reset(); }

void BigMuffEffect::reset() {
  _dc_x1 = _dc_y1 = 0.0f;

  _hp1_lp = 0.0f;
  _hp2_lp = 0.0f;
  _hp3_lp = 0.0f;

  _preLP  = 0.0f;
  _postLP = 0.0f;
  _toneLP = 0.0f;

  _osLP   = 0.0f;

  _xPrev  = 0.0f;
}

float BigMuffEffect::clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

float BigMuffEffect::lerp(float a, float b, float t) { return a + (b - a) * t; }

int16_t BigMuffEffect::clamp16(int32_t x) {
  if (x > 32767) return 32767;
  if (x < -32768) return -32768;
  return (int16_t)x;
}

// DC blocker
float BigMuffEffect::dcBlock(float x, float& x1, float& y1, float R) {
  float y = x - x1 + R * y1;
  x1 = x;
  y1 = y;
  return y;
}

// one pole LP
float BigMuffEffect::onePoleLP(float x, float& y, float a) {
  y = y + a * (x - y);
  return y;
}

// HP via LP (cheap "coupling cap" vibe)
float BigMuffEffect::onePoleHP_viaLP(float x, float& lpState, float a) {
  lpState = lpState + a * (x - lpState);
  return x - lpState;
}

// atan sat (k = hardness)
float BigMuffEffect::satAtan(float x, float k) {
  return (2.0f / 3.14159265f) * atanf(k * x);
}

// tiny safety clamp
float BigMuffEffect::softLimit(float x) {
  if (x > 1.2f)  x = 1.2f;
  if (x < -1.2f) x = -1.2f;
  return x;
}

void BigMuffEffect::processMonoWet(int16_t* mono, int n, float fs, const Params& pIn) {
  float drive = clamp01(pIn.drive);
  float tone  = clamp01(pIn.tone);
  float shape = clamp01(pIn.shape);
  float pres  = clamp01(pIn.pres);

  // **************************
  // oversample (2x)
  // *****************
  float fs2 = fs * 2.0f;

  // **************************
  // gain staging
  // *****************
  float g1 = lerp(1.4f, 5.5f, drive);
  float g2 = lerp(1.4f, 6.5f, drive);
  float g3 = lerp(1.2f, 4.8f, drive);

  // input pad (keeps it "muff" instead of brick)
  float inPad = lerp(0.55f, 0.22f, drive);

  // sat hardness
  float kBase = lerp(2.0f, 6.0f, drive);
  float kHard = lerp(1.8f, 2.8f, shape);
  float k1 = kBase * kHard;
  float k2 = (kBase * 1.1f) * kHard;
  float k3 = (kBase * 0.9f) * kHard;

  // tiny bias (keep it subtle)
  float bias = lerp(0.00f, 0.04f, shape);

  // **************************
  // "coupling caps" (HP)
  // *****************
  float hpA1 = lerp(0.0045f, 0.012f, drive);
  float hpA2 = lerp(0.0040f, 0.010f, drive);
  float hpA3 = lerp(0.0035f, 0.009f, drive);

  // **************************
  // bandlimits (alias control)
  // *****************
  float preHz = lerp(2600.0f, 900.0f, drive);
  preHz = lerp(preHz, 700.0f, pres); // pres up can get nasty if pre stays too bright
  float preA = preHz / fs2;
  if (preA < 0.001f) preA = 0.001f;
  if (preA > 0.45f)  preA = 0.45f;

  float toneHz = lerp(650.0f, 2200.0f, pres);
  float toneA = toneHz / fs2;
  if (toneA < 0.001f) toneA = 0.001f;
  if (toneA > 0.45f)  toneA = 0.45f;

  float postHz = lerp(1600.0f, 4200.0f, pres);
  float postA = postHz / fs2;
  if (postA < 0.001f) postA = 0.001f;
  if (postA > 0.45f)  postA = 0.45f;

  // AA before decimate
  float osCutHz = 12000.0f;
  float osA = osCutHz / fs2;
  if (osA < 0.001f) osA = 0.001f;
  if (osA > 0.45f)  osA = 0.45f;

  // leave headroom (main handles volume)
  float outScale = 0.55f;

  for (int i = 0; i < n; i++) {
    float x1 = (float)mono[i] / 32768.0f;
    float x0 = _xPrev;
    float xHalf = 0.5f * (x0 + x1); // cheap 2x interp

    float yOut = 0.0f;

    // **************************
    // 2x loop + decimate
    // *****************
    for (int os = 0; os < 2; os++) {
      float x = (os == 0) ? xHalf : x1;

      x *= inPad;

      // DC cleanup (slow, but fine)
      x = dcBlock(x, _dc_x1, _dc_y1, 0.995f);

      // pre LP before clipping (big alias win)
      x = onePoleLP(x, _preLP, preA);

      // ******** stage 1 ********
      float s1 = x * g1 + bias;
      float y1 = satAtan(s1, k1);
      y1 = onePoleHP_viaLP(y1, _hp1_lp, hpA1);

      // ******** stage 2 ********
      float s2 = y1 * g2;
      float y2 = satAtan(s2, k2);
      y2 = onePoleHP_viaLP(y2, _hp2_lp, hpA2);

      // ******** stage 3 ********
      float s3 = y2 * g3;
      float y3 = satAtan(s3, k3);
      y3 = onePoleHP_viaLP(y3, _hp3_lp, hpA3);

      // **************************
      // tone (LP/HP blend)
      // *****************
      float lp = onePoleLP(y3, _toneLP, toneA);
      float hp = y3 - lp;
      float yt = (1.0f - tone) * lp + tone * hp;

      // fizz killer
      yt = onePoleLP(yt, _postLP, postA);

      // keep it from going insane
      yt = softLimit(yt);

      // AA LP before decimate
      float ytAA = onePoleLP(yt, _osLP, osA);

      if (os == 1) {
        yOut = ytAA * outScale;
      }
    }

    _xPrev = x1;

    int32_t out = (int32_t)(yOut * 32767.0f);
    mono[i] = clamp16(out);
  }
}
