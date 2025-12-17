# DSP Multi-Effect Guitar Pedal

A custom digital guitar effects pedal using a **Teensy 4.0**, built on top of the **Teensy Audio Library**.  
All effects are written in C++ and designed to run in real time with low latency.

Each effect has its own LED color and uses the same set of 5 knobs, with the meaning of each knob changing depending on the active effect.

**Created by Alec Ventresca**

---

## Effect Cycle Order

The pedal cycles through effects in the following order:

1. Big Muff  
2. Octave  
3. Orchestra / Shimmer  
4. Leslie / Rotary  
5. Bitcrusher  
6. Tremolo  
7. Flanger  
8. Chorus

---

## Effects & Controls

### Big Muff (Red)
A multi-stage fuzz inspired by classic Big Muff-style circuits.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Tone |
| P3  | Drive / Sustain |
| P4  | Shape (clip hardness) |
| P5  | Presence / fizz control |

---

### Octave (Green)
Monophonic octave up/down with adjustable tracking and character.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Blend (dry ↔ wet) |
| P3  | Mix (down ↔ both ↔ up) |
| P4  | Tracking stability |
| P5  | Character (wave shape + up drive) |

---

### Orchestra / Shimmer (Blue)
Shimmer-style reverb for pad-like and orchestral textures.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Mix |
| P3  | Size (reverb length / feedback) |
| P4  | Swell |
| P5  | Tone (feedback brightness) |

---

### Leslie / Rotary (Yellow)
Rotary speaker simulation with horn and drum motion.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Blend |
| P3  | Speed (slow ↔ fast) |
| P4  | Depth |
| P5  | Ramp (acceleration / inertia) |

---

### Bitcrusher (Purple)
Bit depth and sample-rate reduction.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Mix |
| P3  | Bit depth |
| P4  | Sample-rate reduction |
| P5  | Unused |

---

### Tremolo (Cyan)
Classic amplitude modulation using a sine LFO.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Mix |
| P3  | Rate |
| P4  | Depth |
| P5  | Unused |

---

### Flanger (Orange)
Short modulated delay with feedback.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Mix |
| P3  | Rate |
| P4  | Depth |
| P5  | Feedback |

---

### Chorus (White)
Classic chorus using a modulated short delay for widening and movement.

| Knob | Function |
|-----|----------|
| P1  | Volume |
| P2  | Mix |
| P3  | Rate |
| P4  | Depth |
| P5  | Unused |

---

## Hardware

- Teensy 4.0  
- Teensy Audio Shield  
- 5 analog potentiometers  
- Effect selection button  
- RGB LED for effect indication  

---
