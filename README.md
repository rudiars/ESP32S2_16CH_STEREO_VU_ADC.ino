# ESP32-S2 16CH Stereo VU Meter — Internal ADC

## Hardware

- ESP32-S2 Mini
- 2× CD74HC4067
- Internal ESP32-S2 ADC
- HUB08 64×16 RGY LED matrix
- 16 stereo channels = 32 VU meters

This version does **not** use PCM1802, I2S, or FFT.

## Architecture

```text
16 x LEFT  -> 4067-L -> ESP32-S2 ADC LEFT
16 x RIGHT -> 4067-R -> ESP32-S2 ADC RIGHT

S0..S3 are shared between both 4067.
```

Address:

```text
0  = CH1
1  = CH2
...
15 = CH16
```

## 4067 wiring

```text
ESP32-S2    4067-L    4067-R
S0 GPIO21     S0        S0
S1 GPIO34     S1        S1
S2 GPIO35     S2        S2
S3 GPIO36     S3        S3

GPIO37        EN
GPIO38                  EN
```

COM:

```text
4067-L COM -> GPIO17 ADC LEFT
4067-R COM -> GPIO18 ADC RIGHT
```

**Verify all GPIOs against the exact ESP32-S2 Mini board before wiring.**

## Audio input — important

Do NOT connect bipolar audio directly to the ADC.

The audio must be AC-coupled and biased around approximately 1.65 V:

```text
Audio -> coupling capacitor -> bias 1.65 V -> 4067 -> ADC
```

Recommended:

```text
Audio
  |
  +-- buffer/op-amp -- AC coupling -- 1.65V bias -- 4067
                                                        |
                                                        +--> ADC
```

A buffer is recommended for 32 inputs to improve impedance, settling and crosstalk.

## ADC

Default:

```cpp
analogReadResolution(12);
#define ADC_ATTENUATION ADC_11db
```

ADC pins:

```cpp
#define ADC_LEFT_PIN 17
#define ADC_RIGHT_PIN 18
```

Bias:

```cpp
#define VBIAS_V 1.65f
```

If the measured bias is different, change `VBIAS_V`.

The exact ADC voltage range and attenuation behavior depend on the ESP32-S2 and Arduino-ESP32 version, so calibration is recommended.

## RMS measurement

For every channel:

```text
select 4067 channel
     ↓
settle 100 us
     ↓
discard 8 samples
     ↓
take 32 samples
     ↓
RMS
     ↓
dB
     ↓
attack/release
```

Parameters:

```cpp
#define MUX_SETTLE_US 100
#define DISCARD_SAMPLES 8
#define RMS_SAMPLES 32
```

## VU scale

Display range:

```text
-50 dB ... 0 dB
```

Colors:

```text
GREEN  = normal
YELLOW = high
RED    = peak/top
```

Each stereo channel uses four columns:

```text
CH1:  L L R R
CH2:  L L R R
...
CH16: L L R R
```

Total:

```text
16 × 4 = 64 columns
```

## Noise gate

Default:

```cpp
#define NOISE_GATE 0.006f
```

If the meter moves with no signal, increase it.

If low-level signals disappear, decrease it.

## Attack / release

```cpp
const float ATTACK  = 0.55f;
const float RELEASE = 0.18f;
```

Higher ATTACK = faster rise.

Lower RELEASE = slower fall.

## Peak hold

```cpp
const unsigned long PEAK_HOLD_MS = 700;
const float PEAK_DECAY = 0.03f;
```

Each channel has independent L/R peak values.

## HUB08 wiring

```text
A    -> GPIO1
B    -> GPIO2
C    -> GPIO3
D    -> GPIO4
OE   -> GPIO5

RD1  -> GPIO6
GD1  -> GPIO7
RD2  -> GPIO8
GD2  -> GPIO9

LAT  -> GPIO10
CLK  -> GPIO11
```

Colors:

```text
GREEN  = GD1
RED    = RD2
YELLOW = GD1 + RD2
```

The HUB08 driver uses the previously tested ESP32-S2 mapping, direct GPIO and a refresh task.

## Power

Use an external 5 V supply for the HUB08 panel.

All grounds must be common:

```text
ESP32 GND
4067 GND
analog circuit GND
HUB08 GND
```

Do not power the LED panel from the ESP32 3.3 V supply.

## Advantages over PCM1802

For a VU-only application:

- simpler
- no MCLK/BCLK/LRCK
- no I2S
- no FFT
- lower hardware cost
- sufficient for visual VU metering

## Limitations

The internal ADC is not equivalent to a dedicated audio ADC.

Possible issues:

- ADC noise
- non-linearity
- channel gain differences
- 4067 crosstalk
- mux settling
- bias errors
- limited analog input range

For a VU meter these can be compensated with calibration.

## Recommended next improvement

The best next version would add automatic calibration for all 32 inputs:

```text
1. measure DC bias
2. measure noise floor
3. measure gain
4. store calibration in NVS
5. apply correction per channel
```

That would make the 16 stereo meters much more uniform.

## Arduino IDE

Open:

```text
ESP32S2_16CH_STEREO_VU_ADC.ino
```

No PCM1802 or ArduinoFFT library is required.

Serial monitor:

```text
115200 baud
```

Example:

```text
VU 01:72/65 02:43/40 03:90/87 ... 16:25/21
```

Format:

```text
CH:L/R
```

## Project status

Initial internal-ADC version:

```text
ESP32-S2
   |
   +-- ADC LEFT  <- 4067-L <- 16 LEFT
   |
   +-- ADC RIGHT <- 4067-R <- 16 RIGHT
                  |
                  v
                RMS
                  |
                  v
             32 VU meters
                  |
                  v
              HUB08 64x16
```

No PCM1802. No I2S. No FFT.
