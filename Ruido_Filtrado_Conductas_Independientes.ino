/*
 * ============================================================================
 * RUIDO FILTRADO - VERSIÓN ESTABLE
 * Basado en el sketch que funciona correctamente
 * Usa LowPassFilter (más estable que StateVariable) 
 * Mantiene todas las conductas independientes
 * ============================================================================
 */

#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/whitenoise8192_int8.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <LowPassFilter.h>      // Filtro más estable
#include <EventDelay.h>
#include <mozzi_rand.h>

// ========== ESTRUCTURA PARA CADA CANAL ==========
struct Channel {
  // --- Fuente de ruido principal ---
  Oscil <WHITENOISE8192_NUM_CELLS, AUDIO_RATE> noise;
  
  // --- Filtro pasa-bajos (estable) ---
  LowPassFilter filter;
  
  // --- Oscilador adicional para percusión (onda seno simple) ---
  Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> percOsc;
  
  // --- Máquina de estados (exactamente igual que el sketch funcional) ---
  enum State { SILENCE, ATTACK, SUSTAIN, RELEASE };
  State state;
  
  // --- Parámetros ---
  int cutoff;              // Frecuencia de corte (Hz)
  int targetCutoff;
  int resonance;           // Resonancia (0-255)
  int targetResonance;
  int volume;
  int targetVolume;
  
  // --- Temporizadores ---
  unsigned long stateStartTime;
  int attackDuration;
  int sustainDuration;
  int releaseDuration;
  bool releaseAbrupt;
  
  // --- Cambios dentro del bloque ---
  unsigned long lastParamChange;
  int paramChangeInterval;
  
  // --- Ataques percusivos (opcionales) ---
  bool hasPercussionAttack;
  int percussionDuration;
  int percussionIntensity;
  
  // --- Constructor ---
  Channel() : noise(WHITENOISE8192_DATA), percOsc(SIN2048_DATA) {
    state = SILENCE;
    cutoff = 1000;
    targetCutoff = 1000;
    resonance = 80;        // Resonancia baja para evitar inestabilidad
    targetResonance = 80;
    volume = 0;
    targetVolume = 0;
    attackDuration = 100;
    sustainDuration = 3000;
    releaseDuration = 500;
    releaseAbrupt = false;
    lastParamChange = 0;
    paramChangeInterval = 1000;
    hasPercussionAttack = false;
    percussionDuration = 0;
    percussionIntensity = 0;
    
    // Inicializar filtro
    filter.setResonance(resonance);
    filter.setCutoffFreq(cutoff);
    percOsc.setFreq(440);
  }
  
  // Actualizar filtro con parámetros actuales
  void updateFilter() {
    filter.setCutoffFreq(cutoff);
    filter.setResonance(resonance);
  }
  
  // Generar muestra final
  int getSample() {
    int rawNoise = noise.next();
    int filtered = filter.next(rawNoise);
    return (filtered * volume) / 256;
  }
};

// ========== INSTANCIAR 2 CANALES ==========
Channel leftChannel;
Channel rightChannel;

EventDelay globalClock;

// ============================================================
// Inicializar canal
// ============================================================
void initChannel(Channel& ch, bool isLeft) {
  if (isLeft) {
    ch.cutoff = rand(800, 3000);
    ch.targetCutoff = ch.cutoff;
  } else {
    ch.cutoff = rand(200, 1200);
    ch.targetCutoff = ch.cutoff;
  }
  
  ch.resonance = rand(40, 120);      // Resonancia moderada (estable)
  ch.targetResonance = ch.resonance;
  ch.updateFilter();
  
  ch.attackDuration = rand(50, 400);
  ch.sustainDuration = rand(2000, 8000);
  ch.releaseDuration = rand(300, 2000);
  ch.releaseAbrupt = (rand(1, 101) < 30);
  
  ch.hasPercussionAttack = (rand(1, 101) < 35);
  if (ch.hasPercussionAttack) {
    ch.percussionDuration = rand(50, 250);
    ch.percussionIntensity = rand(150, 255);
  }
  
  ch.paramChangeInterval = rand(800, 4000);
  
  ch.state = Channel::SILENCE;
  ch.volume = 0;
  ch.targetVolume = 0;
  ch.stateStartTime = millis();
}

// ============================================================
// Iniciar ataque
// ============================================================
void startAttack(Channel& ch, bool isLeft) {
  ch.state = Channel::ATTACK;
  ch.stateStartTime = millis();
  ch.targetVolume = rand(180, 240);
  
  if (isLeft) {
    ch.targetCutoff = rand(600, 3500);
    ch.cutoff = rand(200, 1500);
  } else {
    ch.targetCutoff = rand(150, 1800);
    ch.cutoff = rand(80, 600);
  }
  
  ch.targetResonance = rand(50, 130);
  ch.resonance = rand(40, 100);
  ch.updateFilter();
  
  ch.lastParamChange = millis();
}

// ============================================================
// Actualizar estado del canal
// ============================================================
void updateChannel(Channel& ch, bool isLeft) {
  unsigned long now = millis();
  unsigned long elapsed = now - ch.stateStartTime;
  
  switch(ch.state) {
    case Channel::SILENCE:
      if (globalClock.ready() && rand(1, 101) < 18) {
        startAttack(ch, isLeft);
      }
      break;
      
    case Channel::ATTACK:
      if (elapsed < (unsigned long)ch.attackDuration) {
        float t = (float)elapsed / ch.attackDuration;
        ch.volume = (ch.targetVolume * t);
        ch.cutoff = ch.targetCutoff * t;
        ch.resonance = ch.targetResonance * t;
        ch.updateFilter();
      } else {
        ch.state = Channel::SUSTAIN;
        ch.stateStartTime = now;
        ch.volume = ch.targetVolume;
        ch.cutoff = ch.targetCutoff;
        ch.resonance = ch.targetResonance;
        ch.updateFilter();
      }
      break;
      
    case Channel::SUSTAIN:
      if (now - ch.lastParamChange > (unsigned long)ch.paramChangeInterval) {
        if (isLeft) {
          ch.targetCutoff = rand(400, 4000);
        } else {
          ch.targetCutoff = rand(100, 2200);
        }
        ch.targetResonance = rand(50, 140);
        
        ch.lastParamChange = now;
        ch.paramChangeInterval = rand(800, 5000);
      }
      
      // Transición suave de parámetros (300ms)
      unsigned long paramElapsed = now - ch.lastParamChange;
      if (paramElapsed < 300) {
        float t = (float)paramElapsed / 300;
        ch.cutoff = ch.cutoff + (ch.targetCutoff - ch.cutoff) * t;
        ch.resonance = ch.resonance + (ch.targetResonance - ch.resonance) * t;
        ch.updateFilter();
      }
      
      if (elapsed > (unsigned long)ch.sustainDuration) {
        ch.state = Channel::RELEASE;
        ch.stateStartTime = now;
        ch.releaseAbrupt = (rand(1, 101) < 30);
        
        if (ch.releaseAbrupt) {
          ch.releaseDuration = rand(10, 80);
        } else {
          ch.releaseDuration = rand(400, 1800);
        }
      }
      break;
      
    case Channel::RELEASE:
      if (elapsed < (unsigned long)ch.releaseDuration) {
        if (ch.releaseAbrupt) {
          float t = (float)elapsed / ch.releaseDuration;
          ch.volume = ch.targetVolume * (1 - (t * t));
          ch.cutoff = ch.cutoff * (1 - t);
          ch.updateFilter();
        } else {
          ch.volume = ch.targetVolume * (1 - (float)elapsed / ch.releaseDuration);
        }
        if (ch.volume < 0) ch.volume = 0;
      } else {
        ch.state = Channel::SILENCE;
        ch.volume = 0;
        ch.targetVolume = 0;
        
        if (isLeft) {
          ch.cutoff = rand(600, 3500);
        } else {
          ch.cutoff = rand(150, 1800);
        }
        ch.targetCutoff = ch.cutoff;
        ch.sustainDuration = rand(2000, 8000);
        ch.hasPercussionAttack = (rand(1, 101) < 35);
      }
      break;
  }
}

// ============================================================
// Aplicar efectos percusivos (opcional)
// ============================================================
int applyPercussion(Channel& ch, int sample) {
  if (ch.state == Channel::ATTACK && ch.hasPercussionAttack) {
    unsigned long elapsed = millis() - ch.stateStartTime;
    if (elapsed < (unsigned long)ch.percussionDuration) {
      int rawNoise = ch.noise.next();
      float t = (float)elapsed / ch.percussionDuration;
      int intensity = ch.percussionIntensity * (1 - t);
      int percussion = (rawNoise * intensity) / 256;
      sample = (sample * (1 - t) + percussion * t);
    }
  }
  return sample;
}

// ============================================================
// updateControl()
// ============================================================
void updateControl() {
  updateChannel(leftChannel, true);
  updateChannel(rightChannel, false);
}

// ============================================================
// updateAudio()
// ============================================================
AudioOutput_t updateAudio() {
  int leftSample = 0;
  int rightSample = 0;
  
  if (leftChannel.state != Channel::SILENCE) {
    leftSample = leftChannel.getSample();
    leftSample = applyPercussion(leftChannel, leftSample);
  }
  
  if (rightChannel.state != Channel::SILENCE) {
    rightSample = rightChannel.getSample();
    rightSample = applyPercussion(rightChannel, rightSample);
  }
  
  return StereoOutput::from16Bit(leftSample * 256, rightSample * 256);
}

// ============================================================
// setup()
// ============================================================
void setup() {
  randSeed(analogRead(A0));
  
  initChannel(leftChannel, true);
  initChannel(rightChannel, false);
  
  globalClock.start(500);
  
  startMozzi(16384);
}

// ============================================================
// loop()
// ============================================================
void loop() {
  audioHook();
}