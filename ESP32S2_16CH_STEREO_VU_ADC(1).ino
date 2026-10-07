/*
 ESP32-S2 + 2x CD74HC4067 + INTERNAL ADC
 16 CHANNEL STEREO VU METER / HUB08 64x16

 16 LEFT  -> 4067-L -> ADC_LEFT_PIN
 16 RIGHT -> 4067-R -> ADC_RIGHT_PIN

 IMPORTANT:
 - No PCM1802 / no I2S / no FFT.
 - Audio inputs MUST be AC-coupled and DC-biased.
 - Never apply negative voltage to ESP32 ADC.
 - Verify GPIOs against your exact ESP32-S2 Mini.
*/

#include <Arduino.h>
#include "soc/gpio_struct.h"

#define NUM_CH 16

// ---------- ADC ----------
#define ADC_LEFT_PIN   17
#define ADC_RIGHT_PIN  18
#define ADC_BITS      12
#define ADC_MAX       4095.0f
#define ADC_ATTENUATION ADC_11db

// ---------- 2x CD74HC4067 ----------
#define MUX_S0 21
#define MUX_S1 34
#define MUX_S2 35
#define MUX_S3 36
#define MUX_EN_L 37
#define MUX_EN_R 38

#define VBIAS_V 1.65f
#define ADC_SIGNAL_FULL_SCALE 1.45f
#define MUX_SETTLE_US 100
#define DISCARD_SAMPLES 8
#define RMS_SAMPLES 32

// ---------- VU ----------
#define NOISE_GATE 0.006f
const float ATTACK=0.55f;
const float RELEASE=0.18f;
const unsigned long PEAK_HOLD_MS=700;
const float PEAK_DECAY=0.03f;

float vuL[NUM_CH]={}, vuR[NUM_CH]={};
float peakL[NUM_CH]={}, peakR[NUM_CH]={};

// ---------- HUB08 ----------
#define PIN_A 1
#define PIN_B 2
#define PIN_C 3
#define PIN_D 4
#define PIN_OE 5
#define PIN_RD1 6
#define PIN_GD1 7
#define PIN_RD2 8
#define PIN_GD2 9
#define PIN_LAT 10
#define PIN_CLK 11

#define WIDTH 64
#define HEIGHT 16

#define MASK_A (1UL<<PIN_A)
#define MASK_B (1UL<<PIN_B)
#define MASK_C (1UL<<PIN_C)
#define MASK_D (1UL<<PIN_D)
#define MASK_OE (1UL<<PIN_OE)
#define MASK_RD1 (1UL<<PIN_RD1)
#define MASK_GD1 (1UL<<PIN_GD1)
#define MASK_RD2 (1UL<<PIN_RD2)
#define MASK_GD2 (1UL<<PIN_GD2)
#define MASK_LAT (1UL<<PIN_LAT)
#define MASK_CLK (1UL<<PIN_CLK)
#define HUB_DATA_MASK (MASK_RD1|MASK_GD1|MASK_RD2|MASK_GD2)

uint8_t frameA[HEIGHT][WIDTH], frameB[HEIGHT][WIDTH];
volatile uint8_t (*displayFrame)[WIDTH]=frameA;
uint8_t (*drawFrame)[WIDTH]=frameB;
TaskHandle_t hub08TaskHandle=NULL;

void selectMux(uint8_t ch){
  digitalWrite(MUX_S0,(ch>>0)&1);
  digitalWrite(MUX_S1,(ch>>1)&1);
  digitalWrite(MUX_S2,(ch>>2)&1);
  digitalWrite(MUX_S3,(ch>>3)&1);
}

float adcToAudio(int raw){
  float v=((float)raw/ADC_MAX)*3.3f;
  float a=(v-VBIAS_V)/ADC_SIGNAL_FULL_SCALE;
  return constrain(a,-1.0f,1.0f);
}

void measureChannel(uint8_t ch){
  selectMux(ch);
  delayMicroseconds(MUX_SETTLE_US);

  for(int i=0;i<DISCARD_SAMPLES;i++){
    analogRead(ADC_LEFT_PIN);
    analogRead(ADC_RIGHT_PIN);
  }

  double sl=0,sr=0;
  for(int i=0;i<RMS_SAMPLES;i++){
    float l=adcToAudio(analogRead(ADC_LEFT_PIN));
    float r=adcToAudio(analogRead(ADC_RIGHT_PIN));
    sl+=(double)l*l;
    sr+=(double)r*r;
  }

  float rmsL=sqrtf(sl/RMS_SAMPLES);
  float rmsR=sqrtf(sr/RMS_SAMPLES);

  if(rmsL<NOISE_GATE) rmsL=0;
  if(rmsR<NOISE_GATE) rmsR=0;

  float dbL=20.0f*log10f(max(rmsL,0.0001f));
  float dbR=20.0f*log10f(max(rmsR,0.0001f));

  float l=constrain((dbL+50.0f)/50.0f,0.0f,1.0f);
  float r=constrain((dbR+50.0f)/50.0f,0.0f,1.0f);

  vuL[ch]+=(l>vuL[ch]?ATTACK:RELEASE)*(l-vuL[ch]);
  vuR[ch]+=(r>vuR[ch]?ATTACK:RELEASE)*(r-vuR[ch]);

  if(vuL[ch]>peakL[ch]) peakL[ch]=vuL[ch];
  if(vuR[ch]>peakR[ch]) peakR[ch]=vuR[ch];
}

inline void gpioSet(uint32_t m){GPIO.out_w1ts=m;}
inline void gpioClear(uint32_t m){GPIO.out_w1tc=m;}

void shiftPixel(uint8_t c){
  uint32_t d=0;
  if(c==1)d=MASK_GD1;
  else if(c==2)d=MASK_GD1|MASK_RD2;
  else if(c==3)d=MASK_RD2;
  gpioClear(HUB_DATA_MASK);
  if(d)gpioSet(d);
  gpioSet(MASK_CLK); gpioClear(MASK_CLK);
}

void setRow(uint8_t row){
  uint32_t v=0;
  if(row&1)v|=MASK_A;
  if(row&2)v|=MASK_B;
  if(row&4)v|=MASK_C;
  if(row&8)v|=MASK_D;
  gpioClear(MASK_A|MASK_B|MASK_C|MASK_D);
  if(v)gpioSet(v);
}

void refreshPanelRow(){
  static uint8_t row=0;
  gpioSet(MASK_OE);
  setRow(row);
  for(int x=0;x<WIDTH;x++)shiftPixel(displayFrame[row][x]);
  gpioSet(MASK_LAT); gpioClear(MASK_LAT);
  gpioClear(MASK_OE);
  delayMicroseconds(250);
  gpioSet(MASK_OE);
  if(++row>=HEIGHT)row=0;
}

void hub08RefreshTask(void*){
  while(true){refreshPanelRow();taskYIELD();}
}

uint8_t colorForRow(int y){
  if(y>=13)return 3;
  if(y>=10)return 2;
  return 1;
}

void drawBar(uint8_t x,float level){
  int h=constrain((int)roundf(level*HEIGHT),0,HEIGHT);
  for(int y=0;y<h;y++)
    drawFrame[HEIGHT-1-y][x]=colorForRow(y);
}

void drawPeak(uint8_t x,float level){
  int y=HEIGHT-1-(int)roundf(level*(HEIGHT-1));
  drawFrame[constrain(y,0,HEIGHT-1)][x]=3;
}

void drawVU(){
  memset(drawFrame,0,sizeof(frameB));
  for(int ch=0;ch<NUM_CH;ch++){
    uint8_t x=ch*4;
    drawBar(x,vuL[ch]); drawBar(x+1,vuL[ch]);
    drawBar(x+2,vuR[ch]); drawBar(x+3,vuR[ch]);
    drawPeak(x+1,peakL[ch]);
    drawPeak(x+3,peakR[ch]);
  }
  noInterrupts();
  uint8_t (*old)[WIDTH]=(uint8_t (*)[WIDTH])displayFrame;
  displayFrame=drawFrame;
  drawFrame=old;
  interrupts();
}

void updatePeaks(){
  static unsigned long t=0;
  unsigned long now=millis();
  if(now-t<PEAK_HOLD_MS)return;
  t=now;
  for(int i=0;i<NUM_CH;i++){
    peakL[i]-=PEAK_DECAY; peakR[i]-=PEAK_DECAY;
    if(peakL[i]<vuL[i])peakL[i]=vuL[i];
    if(peakR[i]<vuR[i])peakR[i]=vuR[i];
    if(peakL[i]<0)peakL[i]=0;
    if(peakR[i]<0)peakR[i]=0;
  }
}

void setup(){
  Serial.begin(115200);

  analogReadResolution(ADC_BITS);
  analogSetPinAttenuation(ADC_LEFT_PIN,ADC_ATTENUATION);
  analogSetPinAttenuation(ADC_RIGHT_PIN,ADC_ATTENUATION);

  pinMode(MUX_S0,OUTPUT); pinMode(MUX_S1,OUTPUT);
  pinMode(MUX_S2,OUTPUT); pinMode(MUX_S3,OUTPUT);
  pinMode(MUX_EN_L,OUTPUT); pinMode(MUX_EN_R,OUTPUT);
  digitalWrite(MUX_EN_L,LOW); digitalWrite(MUX_EN_R,LOW);
  selectMux(0);

  int pins[]={PIN_A,PIN_B,PIN_C,PIN_D,PIN_OE,PIN_RD1,PIN_GD1,
              PIN_RD2,PIN_GD2,PIN_LAT,PIN_CLK};
  for(int p:pins)pinMode(p,OUTPUT);
  digitalWrite(PIN_OE,HIGH);
  digitalWrite(PIN_RD1,LOW); digitalWrite(PIN_GD1,LOW);
  digitalWrite(PIN_RD2,LOW); digitalWrite(PIN_GD2,LOW);
  digitalWrite(PIN_LAT,LOW); digitalWrite(PIN_CLK,LOW);

  memset(frameA,0,sizeof(frameA));
  memset(frameB,0,sizeof(frameB));

  xTaskCreate(hub08RefreshTask,"HUB08_REFRESH",2048,NULL,1,&hub08TaskHandle);

  Serial.println("ESP32-S2 16CH STEREO VU - INTERNAL ADC");
  Serial.println("No PCM1802 / No I2S / No FFT");
}

void loop(){
  for(uint8_t ch=0;ch<NUM_CH;ch++)measureChannel(ch);
  updatePeaks();

  static unsigned long disp=0;
  if(millis()-disp>=20){disp=millis();drawVU();}

  static unsigned long dbg=0;
  if(millis()-dbg>=1000){
    dbg=millis();
    Serial.print("VU ");
    for(int i=0;i<NUM_CH;i++)
      Serial.printf("%02d:%d/%d ",i+1,(int)(vuL[i]*100),(int)(vuR[i]*100));
    Serial.println();
  }
}
