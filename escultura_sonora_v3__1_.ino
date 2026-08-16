
/*
  ============================================================
  ESCULTURA SONORA v3
  ESP32 DEVKIT V1 + MOZZI

  PRINCIPIOS DE COMPOSICIÓN:
  - Procesos lentos audibles: barridos de filtro 5-15 segundos
  - Contrapunto activo: si un canal es lento, el otro es rápido
  - Ataques perceptibles: entrada gradual o percusiva clara
  - Cambio de época anunciado: silencio compartido antes del cambio

  HARDWARE:
  - GPIO25 → Canal L  (DAC interno ESP32)
  - GPIO26 → Canal R
  ============================================================
*/

#include <MozziConfigValues.h>
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO

#include <Mozzi.h>
#include <Oscil.h>
#include <tables/whitenoise8192_int8.h>
#include <tables/pinknoise8192_int8.h>
#include <ResonantFilter.h>
#include <EventDelay.h>
#include <mozzi_rand.h>


// ============================================================
// TIPOS — deben ir PRIMERO para que todo el archivo los vea
// ============================================================

enum ChanState { SILENCE, ATTACK, SUSTAIN, RELEASE };

#define MODE_SLOW 0
#define MODE_FAST 1

struct Channel {
  ChanState     state;
  uint8_t       volume;
  uint8_t       targetVolume;
  float         cutoff;
  float         cutoffTarget;
  float         cutoffSpeed;
  uint8_t       resonance;
  int           filterType;
  int           targetFilterType;
  unsigned long stateStart;
  unsigned long lastFilterChange;
  int           attackDuration;
  int           sustainDuration;
  int           releaseDuration;
  bool          percussive;
  int           mode;
};


// ============================================================
// FILTROS
// ============================================================

#define F_LP 0
#define F_HP 1
#define F_BP 2

ResonantFilter<LOWPASS>  lpLeft,  lpRight;
ResonantFilter<HIGHPASS> hpLeft,  hpRight;
ResonantFilter<BANDPASS> bpLeft,  bpRight;


// ============================================================
// OSCILADORES DE RUIDO
// ============================================================

Oscil<WHITENOISE8192_NUM_CELLS, MOZZI_AUDIO_RATE> noiseLeft(WHITENOISE8192_DATA);
Oscil<PINKNOISE8192_NUM_CELLS,  MOZZI_AUDIO_RATE> noiseRight(PINKNOISE8192_DATA);


// ============================================================
// ÉPOCAS
// ============================================================

#define EPOCH_BRIGHT  0
#define EPOCH_DARK    1
#define EPOCH_MIXED   2
#define EPOCH_SPARSE  3
#define EPOCH_DENSE   4

uint8_t       currentEpoch  = EPOCH_MIXED;
unsigned long epochStart    = 0;
unsigned long epochDuration = 30000;

bool          epochTransition = false;
unsigned long transStart      = 0;
#define TRANS_DURATION 4000


// ============================================================
// CANALES E INSTANCIAS GLOBALES
// ============================================================

Channel chanL, chanR;

int modeLeft  = MODE_SLOW;
int modeRight = MODE_FAST;

int probNewSound = 15;

EventDelay globalClock;
#define CLOCK_MS 250


// ============================================================
// HELPERS DE FILTRO
// ============================================================

int applyFilterL(int sample, int type) {
  switch (type) {
    case F_HP: return hpLeft.next(sample);
    case F_BP: return bpLeft.next(sample);
    default:   return lpLeft.next(sample);
  }
}

int applyFilterR(int sample, int type) {
  switch (type) {
    case F_HP: return hpRight.next(sample);
    case F_BP: return bpRight.next(sample);
    default:   return lpRight.next(sample);
  }
}

void syncFiltersL(uint8_t cutoff, uint8_t res) {
  lpLeft.setCutoffFreqAndResonance(cutoff, res);
  hpLeft.setCutoffFreqAndResonance(cutoff, res);
  bpLeft.setCutoffFreqAndResonance(cutoff, res);
}

void syncFiltersR(uint8_t cutoff, uint8_t res) {
  lpRight.setCutoffFreqAndResonance(cutoff, res);
  hpRight.setCutoffFreqAndResonance(cutoff, res);
  bpRight.setCutoffFreqAndResonance(cutoff, res);
}


// ============================================================
// VELOCIDAD DE BARRIDO
// Calcula cuánto moverse por tick para cubrir el rango en N segundos
// ============================================================

float sweepSpeed(float from, float to, float durationSec) {
  float ticks = durationSec * (float)MOZZI_CONTROL_RATE;
  if (ticks < 1.0f) ticks = 1.0f;
  return abs(to - from) / ticks;
}


// ============================================================
// INICIAR EVENTO
// ============================================================

void startAttack(Channel &ch, bool isLeft) {

  ch.state      = ATTACK;
  ch.stateStart = millis();

  if (ch.mode == MODE_FAST) {
    // Percusivo: seco, claro, sin barrido
    ch.percussive      = true;
    ch.attackDuration  = rand(8,   60);
    ch.sustainDuration = rand(150, 800);
    ch.releaseDuration = rand(40,  200);
    ch.targetVolume    = (uint8_t)rand(160, 255);
    ch.cutoffSpeed     = 0.0f;

    if (isLeft) {
      ch.cutoff     = (float)rand(150, 250);
      ch.resonance  = (uint8_t)rand(80, 180);
      ch.filterType = (rand(0, 256) < 128) ? F_HP : F_BP;
    } else {
      ch.cutoff     = (float)rand(50, 130);
      ch.resonance  = (uint8_t)rand(60, 140);
      ch.filterType = (rand(0, 256) < 128) ? F_LP : F_BP;
    }
    ch.cutoffTarget = ch.cutoff;

  } else {
    // Lento: ataque largo, barrido de filtro audible
    ch.percussive      = false;
    ch.attackDuration  = rand(3000, 8000);
    ch.sustainDuration = rand(8000, 20000);
    ch.releaseDuration = rand(4000, 10000);
    ch.targetVolume    = (uint8_t)rand(100, 200);

    float sweepDur = (float)rand(5, 16);

    if (isLeft) {
      bool asc       = (rand(0, 256) < 128);
      ch.cutoff      = asc ? (float)rand(60,  100) : (float)rand(180, 240);
      ch.cutoffTarget= asc ? (float)rand(180, 240) : (float)rand(60,  100);
      ch.resonance   = (uint8_t)rand(60, 150);
      ch.filterType  = (rand(0, 256) < 180) ? F_BP : F_HP;
    } else {
      bool asc       = (rand(0, 256) < 128);
      ch.cutoff      = asc ? (float)rand(20,  60)  : (float)rand(120, 180);
      ch.cutoffTarget= asc ? (float)rand(120, 180) : (float)rand(20,  60);
      ch.resonance   = (uint8_t)rand(40, 120);
      ch.filterType  = (rand(0, 256) < 180) ? F_LP : F_BP;
    }

    ch.cutoffSpeed = sweepSpeed(ch.cutoff, ch.cutoffTarget, sweepDur);
  }

  ch.targetFilterType = ch.filterType;
  ch.lastFilterChange = millis();

  if (isLeft) syncFiltersL((uint8_t)ch.cutoff, ch.resonance);
  else        syncFiltersR((uint8_t)ch.cutoff, ch.resonance);
}


// ============================================================
// TRANSICIÓN DE ÉPOCA
// ============================================================

void checkEpochTransition() {

  unsigned long now = millis();

  if (!epochTransition && (now - epochStart > epochDuration)) {
    epochTransition = true;
    transStart      = now;

    if (chanL.state == SUSTAIN || chanL.state == ATTACK) {
      chanL.state           = RELEASE;
      chanL.stateStart      = now;
      chanL.releaseDuration = 2000;
      chanL.percussive      = false;
    }
    if (chanR.state == SUSTAIN || chanR.state == ATTACK) {
      chanR.state           = RELEASE;
      chanR.stateStart      = now;
      chanR.releaseDuration = 2000;
      chanR.percussive      = false;
    }
    return;
  }

  if (epochTransition && (now - transStart > TRANS_DURATION)) {
    epochTransition = false;
    epochStart      = now;

    currentEpoch  = (uint8_t)rand(0, 5);
    epochDuration = (unsigned long)rand(30, 90) * 1000UL;

    // Invertir modos — contrapunto fresco
    modeLeft  = (modeLeft  == MODE_SLOW) ? MODE_FAST : MODE_SLOW;
    modeRight = (modeRight == MODE_SLOW) ? MODE_FAST : MODE_SLOW;
    chanL.mode = modeLeft;
    chanR.mode = modeRight;

    switch (currentEpoch) {
      case EPOCH_SPARSE: probNewSound = 6;  break;
      case EPOCH_DENSE:  probNewSound = 45; break;
      default:           probNewSound = 18; break;
    }

    Serial.print(">> EPOCA ");
    Serial.print(currentEpoch);
    Serial.print("  L:");
    Serial.print(modeLeft  == MODE_SLOW ? "SLOW" : "FAST");
    Serial.print("  R:");
    Serial.println(modeRight == MODE_SLOW ? "SLOW" : "FAST");
  }
}


// ============================================================
// MÁQUINA DE ESTADOS
// ============================================================

void updateChannel(Channel &ch, bool isLeft) {

  if (epochTransition && (ch.state == ATTACK || ch.state == SUSTAIN)) {
    ch.state          = RELEASE;
    ch.stateStart     = millis();
    ch.releaseDuration= 1500;
    ch.percussive     = false;
  }

  unsigned long now     = millis();
  unsigned long elapsed = now - ch.stateStart;

  switch (ch.state) {

    case SILENCE:
      if (!epochTransition && globalClock.ready()) {
        if (rand(1, 101) < probNewSound) {
          startAttack(ch, isLeft);
          // Contrapunto: el otro canal toma el modo opuesto
          if (isLeft) {
            chanR.mode = (ch.mode == MODE_SLOW) ? MODE_FAST : MODE_SLOW;
          } else {
            chanL.mode = (ch.mode == MODE_SLOW) ? MODE_FAST : MODE_SLOW;
          }
        }
      }
      break;

    case ATTACK:
      if (elapsed < (unsigned long)ch.attackDuration) {
        ch.volume = (uint8_t)(
          (unsigned long)ch.targetVolume * elapsed
          / (unsigned long)ch.attackDuration
        );
      } else {
        ch.state      = SUSTAIN;
        ch.stateStart = now;
        ch.volume     = ch.targetVolume;
      }
      break;

    case SUSTAIN:
      // Barrido continuo del cutoff
      if (ch.cutoffSpeed > 0.0f) {
        if (ch.cutoff < ch.cutoffTarget) {
          ch.cutoff += ch.cutoffSpeed;
          if (ch.cutoff > ch.cutoffTarget) ch.cutoff = ch.cutoffTarget;
        } else if (ch.cutoff > ch.cutoffTarget) {
          ch.cutoff -= ch.cutoffSpeed;
          if (ch.cutoff < ch.cutoffTarget) ch.cutoff = ch.cutoffTarget;
        }
        if (isLeft) syncFiltersL((uint8_t)ch.cutoff, ch.resonance);
        else        syncFiltersR((uint8_t)ch.cutoff, ch.resonance);
      }

      // Cambio de tipo de filtro — sólo en modo lento
      if (ch.mode == MODE_SLOW) {
        if (now - ch.lastFilterChange > (unsigned long)rand(5000, 12000)) {
          if (rand(1, 101) < 35) {
            ch.targetFilterType = rand(0, 3);
          }
          ch.lastFilterChange = now;
        }
        if (ch.filterType != ch.targetFilterType) {
          ch.filterType = ch.targetFilterType;
        }
      }

      if (elapsed > (unsigned long)ch.sustainDuration) {
        ch.state      = RELEASE;
        ch.stateStart = now;
      }
      break;

    case RELEASE:
      if (elapsed < (unsigned long)ch.releaseDuration) {
        if (ch.percussive) {
          if (ch.volume >= 8) ch.volume -= 8; else ch.volume = 0;
        } else {
          if (ch.volume > 0) ch.volume--;
        }
      } else {
        ch.state        = SILENCE;
        ch.volume       = 0;
        ch.targetVolume = 0;
      }
      break;
  }
}


// ============================================================
// updateControl()
// ============================================================

void updateControl() {
  checkEpochTransition();
  updateChannel(chanL, true);
  updateChannel(chanR, false);
  if (globalClock.ready()) {
    globalClock.start(CLOCK_MS);
  }
}


// ============================================================
// updateAudio()
// ============================================================

AudioOutput updateAudio() {

  int rawL = noiseLeft.next();
  int rawR = noiseRight.next();

  int filtL = applyFilterL(rawL, chanL.filterType);
  int filtR = applyFilterR(rawR, chanR.filterType);

  int outL = ((int)filtL * (int)chanL.volume) >> 7;
  int outR = ((int)filtR * (int)chanR.volume) >> 7;

  outL *= 6;
  outR *= 6;

  outL = constrain(outL, -32768, 32767);
  outR = constrain(outR, -32768, 32767);

  return StereoOutput::fromNBit(16, (long)outL, (long)outR);
}


// ============================================================
// setup()
// ============================================================

void setup() {

  Serial.begin(115200);
  randSeed(analogRead(0) ^ analogRead(1) ^ analogRead(2));

  noiseLeft.setFreq(1.777f);
  noiseRight.setFreq(2.333f);

  // Canal L — SLOW (expansivo, barridos largos)
  chanL.state            = SILENCE;
  chanL.volume           = 0;
  chanL.targetVolume     = 0;
  chanL.cutoff           = 180.0f;
  chanL.cutoffTarget     = 180.0f;
  chanL.cutoffSpeed      = 0.0f;
  chanL.resonance        = 80;
  chanL.filterType       = F_HP;
  chanL.targetFilterType = F_HP;
  chanL.stateStart       = 0;
  chanL.lastFilterChange = 0;
  chanL.attackDuration   = 3000;
  chanL.sustainDuration  = 8000;
  chanL.releaseDuration  = 4000;
  chanL.percussive       = false;
  chanL.mode             = MODE_SLOW;

  // Canal R — FAST (percusivo, seco)
  chanR.state            = SILENCE;
  chanR.volume           = 0;
  chanR.targetVolume     = 0;
  chanR.cutoff           = 100.0f;
  chanR.cutoffTarget     = 100.0f;
  chanR.cutoffSpeed      = 0.0f;
  chanR.resonance        = 70;
  chanR.filterType       = F_LP;
  chanR.targetFilterType = F_LP;
  chanR.stateStart       = 0;
  chanR.lastFilterChange = 0;
  chanR.attackDuration   = 100;
  chanR.sustainDuration  = 500;
  chanR.releaseDuration  = 200;
  chanR.percussive       = true;
  chanR.mode             = MODE_FAST;

  syncFiltersL((uint8_t)chanL.cutoff, chanL.resonance);
  syncFiltersR((uint8_t)chanR.cutoff, chanR.resonance);

  modeLeft      = MODE_SLOW;
  modeRight     = MODE_FAST;
  epochStart    = millis();
  epochDuration = 30000;
  currentEpoch  = EPOCH_MIXED;

  globalClock.start(CLOCK_MS);

  startMozzi();

  Serial.println("ESCULTURA SONORA v3 — lista");
  Serial.println(">> EPOCA 2  L:SLOW  R:FAST");
}


// ============================================================
// loop()
// ============================================================

void loop() {
  audioHook();
}
