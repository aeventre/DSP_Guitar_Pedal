#include "OrchestraEffect.h"
#include <math.h>

// **************************
// Helpers
// *****************
float OrchestraEffect::clamp01(float x){
  if(x<0) return 0;
  if(x>1) return 1;
  return x;
}

float OrchestraEffect::lerp(float a,float b,float t){
  return a + (b-a)*t;
}

// Clamp to int16 range
int16_t OrchestraEffect::clamp16(int32_t x){
  if (x > 32767) return 32767;
  if (x < -32768) return -32768;
  return (int16_t)x;
}

// One-pole LP (cheap smoothing)
float OrchestraEffect::onePoleLP(float x, float& y, float a){
  y = y + a * (x - y);
  return y;
}

// HP via LP subtraction (DC removal)
float OrchestraEffect::onePoleHP_viaLP(float x, float& lpState, float a){
  lpState = lpState + a * (x - lpState);
  return x - lpState;
}

// Soft saturation (keep level up without hard clipping)
float OrchestraEffect::softSat(float x, float drive){
  float z = x * drive;
  float y = tanhf(z);
  float n = tanhf(drive);
  return y / (n > 1e-6f ? n : 1.0f);
}

// **************************
// DelayLine
// *****************
void OrchestraEffect::DelayLine::reset(){
  for(int i=0;i<MAX;i++) buf[i]=0.0f;
  w=0;
}

void OrchestraEffect::DelayLine::push(float x){
  buf[w]=x;
  w++; if(w>=MAX) w=0;
}

float OrchestraEffect::DelayLine::readFrac(float dSamp) const{
  float r = (float)w - dSamp;
  while(r<0.0f) r += (float)MAX;
  while(r>=(float)MAX) r -= (float)MAX;

  int i0 = (int)r;
  int i1 = i0+1; if(i1>=MAX) i1=0;

  float f = r - (float)i0;
  return buf[i0] + f*(buf[i1]-buf[i0]);
}

// **************************
// PitchShift (4-grain overlap)
// *****************
void OrchestraEffect::PitchShift::reset(){
  for(int i=0;i<BUF;i++) buf[i]=0.0f;
  w=0;
  ph[0]=0.00f; ph[1]=0.25f; ph[2]=0.50f; ph[3]=0.75f;
}

// Hann window for grain overlap
float OrchestraEffect::PitchShift::hann(float p01){
  return 0.5f - 0.5f*cosf(2.0f * 3.14159265f * p01);
}

float OrchestraEffect::PitchShift::readFrac(float delaySamp) const{
  float r = (float)w - delaySamp;
  while(r<0.0f) r += (float)BUF;
  while(r>=(float)BUF) r -= (float)BUF;

  int i0 = (int)r;
  int i1 = i0+1; if(i1>=BUF) i1=0;

  float f = r - (float)i0;
  return buf[i0] + f*(buf[i1]-buf[i0]);
}

float OrchestraEffect::PitchShift::process(float x, float ratio, float fs, float grainMs){
  buf[w] = x;
  w++; if(w>=BUF) w=0;

  float grain = (grainMs/1000.0f) * fs;
  if(grain < 256.0f) grain = 256.0f;
  if(grain > (float)(BUF-16)) grain = (float)(BUF-16);

  // Time-warp step (ratio controls shift amount)
  float step = (1.0f - ratio) / grain;

  float y = 0.0f;
  float wsum = 0.0f;

  for(int k=0;k<4;k++){
    ph[k] += step;
    while(ph[k] < 0.0f) ph[k] += 1.0f;
    while(ph[k] >= 1.0f) ph[k] -= 1.0f;

    float d = ph[k] * grain;
    float s = readFrac(d);

    float win = hann(ph[k]);
    y += s * win;
    wsum += win;
  }

  if(wsum > 1e-6f) y /= wsum;
  return y;
}

// **************************
// Comb / Allpass
// *****************
void OrchestraEffect::Comb::init(int delay){
  if(delay<1) delay=1;
  if(delay>MAX) delay=MAX;
  len=delay;
  reset();
}

void OrchestraEffect::Comb::reset(){
  for(int i=0;i<len;i++) buf[i]=0.0f;
  idx=0;
  lp=0.0f;
}

float OrchestraEffect::Comb::process(float x, float fb, float damp){
  float y = buf[idx];
  lp = lp + damp*(y - lp);
  buf[idx] = x + fb*lp;
  idx++; if(idx>=len) idx=0;
  return y;
}

void OrchestraEffect::Allpass::init(int delay){
  if(delay<1) delay=1;
  if(delay>MAX) delay=MAX;
  len=delay;
  reset();
}

void OrchestraEffect::Allpass::reset(){
  for(int i=0;i<len;i++) buf[i]=0.0f;
  idx=0;
}

float OrchestraEffect::Allpass::process(float x, float g){
  float b = buf[idx];
  float y = -g*x + b;
  buf[idx] = x + g*y;
  idx++; if(idx>=len) idx=0;
  return y;
}

// **************************
// ShimmerStage
// *****************
void OrchestraEffect::ShimmerStage::init(){
  // Comb delays chosen to avoid obvious ringing
  c[0].init(1557);
  c[1].init(1617);
  c[2].init(1491);
  c[3].init(1422);

  ap[0].init(225);
  ap[1].init(556);

  reset();
}

void OrchestraEffect::ShimmerStage::reset(){
  for(int i=0;i<4;i++) c[i].reset();
  for(int i=0;i<2;i++) ap[i].reset();

  ps.reset();
  fbLP = 0.0f;
  wetLP = 0.0f;
  dcLP = 0.0f;
}

float OrchestraEffect::ShimmerStage::process(float x, float fs,
                                            float size, float tone,
                                            float shimmerAmt, float ratio,
                                            float grainMs)
{
  // MORE SUSTAIN: allow higher feedback
  float fb   = OrchestraEffect::lerp(0.82f, 0.965f, size);
  float damp = OrchestraEffect::lerp(0.08f, 0.42f, size);
  float apg  = OrchestraEffect::lerp(0.68f, 0.78f, size);

  // darker feedback hides pitch artifacts + longer tails
  float fbLPHz = OrchestraEffect::lerp(5200.0f, 1500.0f, tone);
  float fbA = fbLPHz / fs;
  if(fbA < 0.002f) fbA = 0.002f;
  if(fbA > 0.45f)  fbA = 0.45f;

  float sh = OrchestraEffect::clamp01(shimmerAmt);
  if(sh > 0.95f) sh = 0.95f;

  float fbShift = ps.process(x, ratio, fs, grainMs);
  fbShift = OrchestraEffect::onePoleLP(fbShift, fbLP, fbA);

  // shimmer injection
  float in = x + sh * 0.78f * fbShift;

  float csum = 0.0f;
  csum += c[0].process(in, fb, damp);
  csum += c[1].process(in, fb*0.997f, damp);
  csum += c[2].process(in, fb*1.003f, damp);
  csum += c[3].process(in, fb*0.991f, damp);
  csum *= 0.25f;

  float r = ap[1].process(ap[0].process(csum, apg), apg);

  // DC cleanup + light smoothing
  r = OrchestraEffect::onePoleHP_viaLP(r, dcLP, 0.0008f);
  r = OrchestraEffect::onePoleLP(r, wetLP, 0.10f);

  return r;
}

// **************************
// OrchestraEffect
// *****************
OrchestraEffect::OrchestraEffect(){
  _pre.reset();
  _up.init();
  _down.init();
  reset();
}

void OrchestraEffect::reset(){
  _pre.reset();
  _env = 0.0f;
  _duck = 1.0f;
  _swellLP = 0.0f;

  _up.reset();
  _down.reset();

  _outLP = 0.0f;
  _outDC = 0.0f;
}

void OrchestraEffect::processMono(const int16_t* inMono, int16_t* outMono, int n, float fs, const Params& pIn){
  float mix   = clamp01(pIn.mix);
  float size  = clamp01(pIn.size);
  float swell = clamp01(pIn.swell);
  float upAmt = clamp01(pIn.up);
  float dnAmt = clamp01(pIn.down);
  float tone  = clamp01(pIn.tone);

  // Predelay scales with size
  float preMs = lerp(10.0f, 32.0f, size);
  float preS  = (preMs/1000.0f) * fs;

  // Envelope follower
  float envA = 0.03f;
  float envR = 0.0010f;

  float duckAtk = lerp(0.05f, 0.12f, swell);
  float duckRel = lerp(0.0012f, 0.00018f, swell); // MORE SUSTAIN

  float openTh  = 0.030f;

  // Bigger grains = smoother shimmer
  float grainMsUp = lerp(50.0f, 86.0f, size);
  float grainMsDn = lerp(56.0f, 96.0f, size);

  // LOUDER: bigger wet gain
  float wetGain = lerp(2.4f, 4.2f, upAmt);

  for(int i=0;i<n;i++){
    float x = (float)inMono[i] / 32768.0f;
    float dry = x;

    float ax = fabsf(x);
    _env = _env + ((ax > _env) ? envA : envR) * (ax - _env);

    float playing = (_env > openTh) ? 1.0f : 0.0f;

    float wetFloor = lerp(1.0f, 0.22f, swell);

    float targetDuck = playing ? wetFloor : 1.0f;
    float a = (targetDuck > _duck) ? duckRel : duckAtk;
    _duck = _duck + a * (targetDuck - _duck);

    float swellGain = _duck;
    swellGain *= swellGain;
    swellGain = onePoleLP(swellGain, _swellLP, 0.02f);

    _pre.push(x);
    float pre = _pre.readFrac(preS);

    float inWet = pre * swellGain;

    float yUp = _up.process(inWet, fs, size, tone, upAmt, 2.0f, grainMsUp);
    float yDn = _down.process(yUp,  fs, size, tone, dnAmt, 0.5f, grainMsDn);

    float wet = yDn * wetGain;

    // keep hot but not fuzzy
    wet = softSat(wet, 0.78f);
    wet = onePoleHP_viaLP(wet, _outDC, 0.0008f);
    wet = onePoleLP(wet, _outLP, 0.08f);

    float out = (1.0f - mix) * dry + mix * wet;
    outMono[i] = clamp16((int32_t)(out * 32767.0f));
  }
}
