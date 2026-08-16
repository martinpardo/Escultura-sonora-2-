/*
 * ============================================================================
 * INSTALACIÓN SONORA ESTÉREO - 2 CANALES CON CONDUCTAS INDEPENDIENTES
 * Basado en Mozzi para Arduino Uno/Nano
 * 
 * VERSIÓN CORREGIDA - Includes verificados
 * ============================================================================
 */

// ========== CONFIGURACIÓN ESTÉREO - DEBE IR ANTES DE MOZZI ==========
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
// ======================================================================

#include <Mozzi.h>
#include <Oscil.h>
// TABLAS CORRECTAS (nombres en minúsculas como existen en la librería)
#include <tables/sin2048_int8.h>        // Onda sinusoidal
#include <tables/saw2048_int8.h>        // Diente de sierra
#include <tables/triangle2048_int8.h>   // Triangular
#include <tables/whitenoise8192_int8.h> // Ruido blanco
// NOTA: No incluimos square porque no todas las versiones de Mozzi la tienen
// Usaremos combinaciones de las anteriores para variar timbres

#include <EventDelay.h>
#include <mozzi_rand.h>

// ========== DEFINICIÓN DE TIPOS DE ONDA ==========
// Usamos solo los tipos disponibles en Mozzi estándar
enum WaveType {
  WAVE_SINE,
  WAVE_SAW,
  WAVE_TRIANGLE,
  WAVE_NOISE
};

// ========== ESTRUCTURA PARA CADA CANAL ==========
struct Channel {
  // --- Osciladores para diferentes timbres ---
  Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> oscSine;
  Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw;
  Oscil <TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTriangle;
  Oscil <WHITENOISE8192_NUM_CELLS, AUDIO_RATE> oscNoise;
  
  // --- Máquina de estados ---
  enum State { SILENCE, ATTACK, SUSTAIN, RELEASE };
  State state;
  
  // --- Parámetros sonoros ---
  int frequency;
  int baseFrequency;
  int volume;
  int targetVolume;
  WaveType currentWave;
  
  // --- Temporizadores y duraciones ---
  unsigned long stateStartTime;
  int attackDuration;
  int sustainDuration;
  int releaseDuration;
  bool releaseAbrupt;
  
  // --- Cambios de timbre dentro del bloque ---
  unsigned long lastTimbreChange;
  int timbreChangeInterval;
  WaveType nextWave;
  
  // --- Ataques percusivos ---
  bool hasPercussionAttack;
  int percussionDuration;
  int percussionIntensity;
  
  // --- Constructor ---
  Channel() : 
    oscSine(SIN2048_DATA),
    oscSaw(SAW2048_DATA),
    oscTriangle(TRIANGLE2048_DATA),
    oscNoise(WHITENOISE8192_DATA) {
    state = SILENCE;
    frequency = 440;
    baseFrequency = 440;
    volume = 0;
    targetVolume = 0;
    currentWave = WAVE_SINE;
    attackDuration = 100;
    sustainDuration = 3000;
    releaseDuration = 500;
    releaseAbrupt = false;
    lastTimbreChange = 0;
    timbreChangeInterval = 1000;
    hasPercussionAttack = false;
    percussionDuration = 0;
    percussionIntensity = 0;
  }
  
  // Generar muestra según el tipo de onda actual
  int getSample() {
    switch(currentWave) {
      case WAVE_SINE:    return oscSine.next();
      case WAVE_SAW:     return oscSaw.next();
      case WAVE_TRIANGLE: return oscTriangle.next();
      case WAVE_NOISE:   return oscNoise.next();
      default:           return oscSine.next();
    }
  }
  
  // Cambiar el tipo de onda
  void setWave(WaveType wave) {
    currentWave = wave;
  }
  
  // Establecer frecuencia en todos los osciladores
  void setFrequency(int freq) {
    frequency = freq;
    oscSine.setFreq(freq);
    oscSaw.setFreq(freq);
    oscTriangle.setFreq(freq);
    // Noise no tiene frecuencia
  }
};

// ========== INSTANCIAR 2 CANALES ==========
Channel leftChannel;   // Canal Izquierdo - AGUDOS
Channel rightChannel;  // Canal Derecho - GRAVES

// ========== VARIABLES GLOBALES ==========
EventDelay globalClock;

// ============================================================
// Inicializar un canal con parámetros aleatorios
// ============================================================
void initChannel(Channel& ch, bool isLeft) {
  if (isLeft) {
    ch.baseFrequency = rand(600, 3500);
  } else {
    ch.baseFrequency = rand(50, 300);
  }
  
  ch.setFrequency(ch.baseFrequency);
  ch.currentWave = (WaveType)rand(0, 4);  // 0 a 3 (4 tipos)
  ch.attackDuration = rand(50, 400);
  ch.sustainDuration = rand(1000, 8000);
  ch.releaseDuration = rand(300, 2000);
  
  ch.releaseAbrupt = (rand(1, 101) < 30);
  ch.hasPercussionAttack = (rand(1, 101) < 40);
  
  if (ch.hasPercussionAttack) {
    ch.percussionDuration = rand(50, 300);
    ch.percussionIntensity = rand(150, 255);
  }
  
  ch.timbreChangeInterval = rand(1000, 5000);
  
  ch.state = Channel::SILENCE;
  ch.volume = 0;
  ch.targetVolume = 0;
  ch.stateStartTime = millis();
}

// ============================================================
// Iniciar un ataque en un canal
// ============================================================
void startAttack(Channel& ch, bool isLeft) {
  ch.state = Channel::ATTACK;
  ch.stateStartTime = millis();
  ch.targetVolume = rand(180, 255);
  
  if (rand(1, 101) < 50) {
    int variation = rand(-100, 101);
    int newFreq = ch.baseFrequency + variation;
    
    if (isLeft) {
      newFreq = constrain(newFreq, 400, 4000);
    } else {
      newFreq = constrain(newFreq, 40, 400);
    }
    ch.setFrequency(newFreq);
  }
  
  ch.lastTimbreChange = millis();
  ch.nextWave = (WaveType)rand(0, 4);
}

// ============================================================
// Actualizar el estado de un canal
// ============================================================
void updateChannel(Channel& ch, bool isLeft) {
  unsigned long now = millis();
  unsigned long elapsed = now - ch.stateStartTime;
  
  switch(ch.state) {
    case Channel::SILENCE:
      if (globalClock.ready() && rand(1, 101) < 15) {
        startAttack(ch, isLeft);
      }
      break;
      
    case Channel::ATTACK:
      if (elapsed < (unsigned long)ch.attackDuration) {
        ch.volume = (ch.targetVolume * elapsed) / ch.attackDuration;
      } else {
        ch.state = Channel::SUSTAIN;
        ch.stateStartTime = now;
        ch.volume = ch.targetVolume;
      }
      break;
      
    case Channel::SUSTAIN:
      if (now - ch.lastTimbreChange > (unsigned long)ch.timbreChangeInterval) {
        ch.setWave(ch.nextWave);
        ch.lastTimbreChange = now;
        ch.nextWave = (WaveType)rand(0, 4);
        ch.timbreChangeInterval = rand(1000, 5000);
        
        if (rand(1, 101) < 40) {
          int variation = rand(-80, 81);
          int newFreq = constrain(ch.frequency + variation, 
                                  isLeft ? 400 : 40, 
                                  isLeft ? 4000 : 400);
          ch.setFrequency(newFreq);
        }
      }
      
      if (elapsed > (unsigned long)ch.sustainDuration) {
        ch.state = Channel::RELEASE;
        ch.stateStartTime = now;
        ch.releaseAbrupt = (rand(1, 101) < 30);
        
        if (ch.releaseAbrupt) {
          ch.releaseDuration = rand(10, 100);
        } else {
          ch.releaseDuration = rand(500, 2000);
        }
      }
      break;
      
    case Channel::RELEASE:
      if (elapsed < (unsigned long)ch.releaseDuration) {
        if (ch.releaseAbrupt) {
          float t = (float)elapsed / ch.releaseDuration;
          ch.volume = ch.targetVolume * (1 - (t * t));
        } else {
          ch.volume = ch.targetVolume * (1 - (float)elapsed / ch.releaseDuration);
        }
        if (ch.volume < 0) ch.volume = 0;
      } else {
        ch.state = Channel::SILENCE;
        ch.volume = 0;
        ch.targetVolume = 0;
        
        if (isLeft) {
          ch.baseFrequency = rand(600, 3500);
        } else {
          ch.baseFrequency = rand(50, 300);
        }
        ch.setFrequency(ch.baseFrequency);
        ch.sustainDuration = rand(1000, 8000);
        ch.hasPercussionAttack = (rand(1, 101) < 40);
        if (ch.hasPercussionAttack) {
          ch.percussionDuration = rand(50, 300);
          ch.percussionIntensity = rand(150, 255);
        }
      }
      break;
  }
}

// ============================================================
// Aplicar efectos percusivos al ataque
// ============================================================
int applyPercussion(Channel& ch, int sample) {
  if (ch.state == Channel::ATTACK && ch.hasPercussionAttack) {
    unsigned long elapsed = millis() - ch.stateStartTime;
    if (elapsed < (unsigned long)ch.percussionDuration) {
      int noise = ch.oscNoise.next();
      float t = (float)elapsed / ch.percussionDuration;
      int intensity = ch.percussionIntensity * (1 - t);
      int percussion = (noise * intensity) / 256;
      sample = (sample + percussion) / 2;
    }
  }
  return sample;
}

// ============================================================
// updateControl() - Decisiones compositivas
// ============================================================
void updateControl() {
  updateChannel(leftChannel, true);
  updateChannel(rightChannel, false);
}

// ============================================================
// updateAudio() - Generación de audio estéreo
// ============================================================
AudioOutput_t updateAudio() {
  int leftSample = 0;
  int rightSample = 0;
  
  if (leftChannel.state != Channel::SILENCE) {
    int rawSample = leftChannel.getSample();
    int scaledSample = (rawSample * leftChannel.volume) / 256;
    leftSample = applyPercussion(leftChannel, scaledSample);
  }
  
  if (rightChannel.state != Channel::SILENCE) {
    int rawSample = rightChannel.getSample();
    int scaledSample = (rawSample * rightChannel.volume) / 256;
    rightSample = applyPercussion(rightChannel, scaledSample);
  }
  
  return StereoOutput::from16Bit(leftSample * 256, rightSample * 256);
}

// ============================================================
// setup() - Configuración inicial
// ============================================================
void setup() {
  randSeed(analogRead(A0));
  
  initChannel(leftChannel, true);
  initChannel(rightChannel, false);
  
  globalClock.start(500);
  
  startMozzi(16384);
}

// ============================================================
// loop() - Bucle principal
// ============================================================
void loop() {
  audioHook();
}