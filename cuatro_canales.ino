/*
 * ============================================================================
 * 4 CANALES INDEPENDIENTES - VOCES AUTÓNOMAS
 * Basado en Mozzi + MCP4922 Dual DAC
 * 
 * Características:
 * - Cada canal tiene su propia máquina de estados
 * - Silencios independientes por canal
 * - Release suave o abrupto al finalizar
 * - Cambios de timbre dentro del mismo bloque sonoro
 * - Ataques percusivos ocasionales
 * ============================================================================
 */

// ========== CONFIGURACIÓN MOZZI ==========
// Usamos modo externo para manejar múltiples canales con DACs
#define MOZZI_AUDIO_MODE MOZZI_OUTPUT_EXTERNAL_TIMED
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/triangle2048_int8.h>
#include <tables/square2048_int8.h>
#include <tables/whitenoise8192_int8.h>
#include <EventDelay.h>
#include <mozzi_rand.h>

// ========== LIBRERÍA PARA DAC MCP4922 ==========
#include <SPI.h>
#include <DAC_MCP49xx.h>

// ========== CONFIGURACIÓN DE DACs ==========
#define SS_PIN_DAC1 10      // Chip Select para DAC #1 (canales 0 y 1)
#define SS_PIN_DAC2 9       // Chip Select para DAC #2 (canales 2 y 3)

DAC_MCP49xx dac1(DAC_MCP49xx::MCP4922, SS_PIN_DAC1);
DAC_MCP49xx dac2(DAC_MCP49xx::MCP4922, SS_PIN_DAC2);

// ========== DEFINICIÓN DE TIPOS DE ONDA ==========
enum WaveType {
  WAVE_SINE,
  WAVE_SAW,
  WAVE_TRIANGLE,
  WAVE_SQUARE,
  WAVE_NOISE
};

// ========== ESTRUCTURA PARA CADA VOZ/CANAL ==========
struct Voice {
  // --- Osciladores (múltiples para cambios de timbre) ---
  Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> oscSine;
  Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> oscSaw;
  Oscil <TRIANGLE2048_NUM_CELLS, AUDIO_RATE> oscTriangle;
  Oscil <SQUARE2048_NUM_CELLS, AUDIO_RATE> oscSquare;
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
  bool releaseAbrupt;      // true = release abrupto, false = release suave
  
  // --- Cambios de timbre dentro del bloque ---
  unsigned long lastTimbreChange;
  int timbreChangeInterval;
  WaveType nextWave;
  
  // --- Ataques percusivos ---
  bool hasPercussionAttack;
  int percussionDuration;
  int percussionIntensity;
  
  // --- Constructor ---
  Voice() : 
    oscSine(SIN2048_DATA),
    oscSaw(SAW2048_DATA),
    oscTriangle(TRIANGLE2048_DATA),
    oscSquare(SQUARE2048_DATA),
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
    int sample = 0;
    switch(currentWave) {
      case WAVE_SINE:
        sample = oscSine.next();
        break;
      case WAVE_SAW:
        sample = oscSaw.next();
        break;
      case WAVE_TRIANGLE:
        sample = oscTriangle.next();
        break;
      case WAVE_SQUARE:
        sample = oscSquare.next();
        break;
      case WAVE_NOISE:
        sample = oscNoise.next();
        break;
    }
    return sample;
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
    oscSquare.setFreq(freq);
    // Noise no tiene frecuencia
  }
};

// ========== INSTANCIAR 4 VOCES ==========
Voice voices[4];

// ========== VARIABLES GLOBALES ==========
EventDelay globalClock;  // Reloj maestro para decisiones compositivas

// ============================================================
// Inicializar una voz con parámetros aleatorios
// ============================================================
void initVoice(int channel) {
  Voice& v = voices[channel];
  
  // Frecuencia base según rango del canal
  switch(channel) {
    case 0: v.baseFrequency = rand(600, 3500); break;  // Agudos
    case 1: v.baseFrequency = rand(300, 1200); break;  // Medios-altos
    case 2: v.baseFrequency = rand(120, 500); break;   // Medios-graves
    case 3: v.baseFrequency = rand(50, 250); break;    // Subgraves
  }
  
  v.setFrequency(v.baseFrequency);
  v.currentWave = (WaveType)rand(0, 4);
  v.attackDuration = rand(50, 400);
  v.sustainDuration = rand(1000, 8000);
  v.releaseDuration = rand(300, 2000);
  
  // 30% de probabilidad de release abrupto
  v.releaseAbrupt = (rand(1, 101) < 30);
  
  // 40% de probabilidad de ataque percusivo
  v.hasPercussionAttack = (rand(1, 101) < 40);
  if (v.hasPercussionAttack) {
    v.percussionDuration = rand(50, 300);
    v.percussionIntensity = rand(150, 255);
  }
  
  // Intervalo para cambios de timbre dentro del bloque (1-5 segundos)
  v.timbreChangeInterval = rand(1000, 5000);
  
  // Iniciar en SILENCIO
  v.state = Voice::SILENCE;
  v.volume = 0;
  v.targetVolume = 0;
  v.stateStartTime = millis();
}

// ============================================================
// Iniciar un ataque en una voz
// ============================================================
void startAttack(int channel) {
  Voice& v = voices[channel];
  
  v.state = Voice::ATTACK;
  v.stateStartTime = millis();
  v.targetVolume = rand(180, 255);
  
  // Posible cambio de frecuencia al atacar
  if (rand(1, 101) < 50) {
    int newFreq = v.baseFrequency + rand(-100, 101);
    newFreq = constrain(newFreq, 50, 4000);
    v.setFrequency(newFreq);
  }
  
  // Programar primer cambio de timbre
  v.lastTimbreChange = millis();
  v.nextWave = (WaveType)rand(0, 4);
}

// ============================================================
// Actualizar el estado de una voz
// ============================================================
void updateVoice(int channel) {
  Voice& v = voices[channel];
  unsigned long now = millis();
  unsigned long elapsed = now - v.stateStartTime;
  
  switch(v.state) {
    case Voice::SILENCE:
      // Decidir si iniciar un nuevo sonido (10-30% de probabilidad por ciclo)
      if (globalClock.ready() && rand(1, 101) < 20) {
        startAttack(channel);
      }
      break;
      
    case Voice::ATTACK:
      // Fade in
      if (elapsed < (unsigned long)v.attackDuration) {
        v.volume = (v.targetVolume * elapsed) / v.attackDuration;
      } else {
        // Termina el ataque, pasa a SOSTEN
        v.state = Voice::SUSTAIN;
        v.stateStartTime = now;
        v.volume = v.targetVolume;
      }
      break;
      
    case Voice::SUSTAIN:
      // Durante el sostenido, cambios de timbre ocasionales
      if (now - v.lastTimbreChange > (unsigned long)v.timbreChangeInterval) {
        v.setWave(v.nextWave);
        v.lastTimbreChange = now;
        v.nextWave = (WaveType)rand(0, 4);
        v.timbreChangeInterval = rand(1000, 5000);
        
        // También puede cambiar sutilmente la frecuencia
        if (rand(1, 101) < 40) {
          int variation = rand(-50, 51);
          int newFreq = constrain(v.frequency + variation, 50, 4000);
          v.setFrequency(newFreq);
        }
      }
      
      // Decidir si terminar el bloque
      if (elapsed > (unsigned long)v.sustainDuration) {
        v.state = Voice::RELEASE;
        v.stateStartTime = now;
        
        // Decidir tipo de release (puede ser diferente al inicial)
        v.releaseAbrupt = (rand(1, 101) < 30);
        
        if (v.releaseAbrupt) {
          v.releaseDuration = rand(10, 100);  // Release abrupto: muy corto
        } else {
          v.releaseDuration = rand(500, 2000); // Release suave: largo
        }
      }
      break;
      
    case Voice::RELEASE:
      // Fade out
      if (elapsed < (unsigned long)v.releaseDuration) {
        if (v.releaseAbrupt) {
          // Release abrupto: caída exponencial
          v.volume = v.targetVolume * (1 - (elapsed * elapsed) / (v.releaseDuration * v.releaseDuration));
        } else {
          // Release suave: caída lineal
          v.volume = v.targetVolume * (1 - (float)elapsed / v.releaseDuration);
        }
        if (v.volume < 0) v.volume = 0;
      } else {
        // Termina, vuelve a SILENCIO
        v.state = Voice::SILENCE;
        v.volume = 0;
        v.targetVolume = 0;
        
        // Preparar nueva configuración para el próximo ciclo
        v.baseFrequency = rand(50, 4000);
        // Respetar rangos por canal
        switch(channel) {
          case 0: v.baseFrequency = constrain(v.baseFrequency, 600, 3500); break;
          case 1: v.baseFrequency = constrain(v.baseFrequency, 300, 1200); break;
          case 2: v.baseFrequency = constrain(v.baseFrequency, 120, 500); break;
          case 3: v.baseFrequency = constrain(v.baseFrequency, 50, 250); break;
        }
        v.setFrequency(v.baseFrequency);
        v.sustainDuration = rand(1000, 8000);
        v.hasPercussionAttack = (rand(1, 101) < 40);
      }
      break;
  }
}

// ============================================================
// Aplicar efectos percusivos al ataque (solo si corresponde)
// ============================================================
int applyPercussion(int channel, int sample) {
  Voice& v = voices[channel];
  
  if (v.state == Voice::ATTACK && v.hasPercussionAttack) {
    unsigned long elapsed = millis() - v.stateStartTime;
    if (elapsed < (unsigned long)v.percussionDuration) {
      // Añadir ruido blanco al inicio del ataque
      int noise = v.oscNoise.next();
      int intensity = v.percussionIntensity * (1 - (float)elapsed / v.percussionDuration);
      int percussion = (noise * intensity) / 256;
      sample = (sample + percussion) / 2;
    }
  }
  
  return sample;
}

// ============================================================
// updateControl() - Actualizar todas las voces
// ============================================================
void updateControl() {
  // Actualizar cada voz independientemente
  for (int i = 0; i < 4; i++) {
    updateVoice(i);
  }
  
  // El reloj global se usa solo como temporizador para decisiones
  // No es crítico, las voces tienen sus propios temporizadores
}

// ============================================================
// updateAudio() - Generar muestra para todos los canales
// ============================================================
int updateAudio() {
  int samples[4];
  
  // Generar muestra para cada canal
  for (int i = 0; i < 4; i++) {
    Voice& v = voices[i];
    
    if (v.state == Voice::SILENCE) {
      samples[i] = 0;
    } else {
      // Obtener muestra del oscilador actual
      int rawSample = v.getSample();
      // Aplicar volumen
      int scaledSample = (rawSample * v.volume) / 256;
      // Aplicar efectos percusivos
      samples[i] = applyPercussion(i, scaledSample);
    }
  }
  
  // Enviar a los DACs (desde audioHook, no desde aquí)
  // La función audioOutput se llama automáticamente por Mozzi
  return 0;  // Valor dummy, usamos audioOutput()
}

// ============================================================
// audioOutput() - Enviar muestras a los DACs
// Esta función es llamada automáticamente por Mozzi
// ============================================================
void audioOutput(const AudioOutput_t& output) {
  // Obtener las muestras de alguna manera...
  // Nota: Este es un enfoque simplificado; necesitamos almacenar las muestras
  // de updateAudio en variables globales. Lo haremos con un array estático.
  
  static int lastSamples[4] = {0, 0, 0, 0};
  
  // En un diseño más pulido, usaríamos un buffer circular o similar
  // Por ahora, asumimos que las muestras se actualizan en updateAudio
  // y se leen aquí. Pero esto no es ideal.
  
  // Enviamos a los DACs
  dac1.outputA((lastSamples[0] >> 4) + 2048);
  dac1.outputB((lastSamples[1] >> 4) + 2048);
  dac2.outputA((lastSamples[2] >> 4) + 2048);
  dac2.outputB((lastSamples[3] >> 4) + 2048);
}

// ============================================================
// Función auxiliar para almacenar muestras
// ============================================================
int currentSamples[4];

// ============================================================
// updateAudio() mejorada - Almacena muestras para audioOutput
// ============================================================
// Nota: Reemplazar la función updateAudio anterior con esta
/*
int updateAudio() {
  for (int i = 0; i < 4; i++) {
    Voice& v = voices[i];
    
    if (v.state == Voice::SILENCE) {
      currentSamples[i] = 0;
    } else {
      int rawSample = v.getSample();
      int scaledSample = (rawSample * v.volume) / 256;
      currentSamples[i] = applyPercussion(i, scaledSample);
    }
  }
  return 0;
}
*/

// ============================================================
// audioOutput() actualizada
// ============================================================
void audioOutput(const AudioOutput_t& output) {
  // Enviar muestras almacenadas a los DACs
  dac1.outputA((currentSamples[0] >> 4) + 2048);
  dac1.outputB((currentSamples[1] >> 4) + 2048);
  dac2.outputA((currentSamples[2] >> 4) + 2048);
  dac2.outputB((currentSamples[3] >> 4) + 2048);
}

// ============================================================
// setup() - Configuración inicial
// ============================================================
void setup() {
  // Semilla aleatoria
  randSeed(analogRead(A0));
  
  // Inicializar SPI y DACs
  SPI.begin();
  dac1.init();
  dac2.init();
  
  #if defined(__AVR_ATmega328P__)
    dac1.setPortWrite(true);
    dac2.setPortWrite(true);
  #endif
  
  // Inicializar las 4 voces
  for (int i = 0; i < 4; i++) {
    initVoice(i);
  }
  
  // Inicializar reloj global (cada 500ms)
  globalClock.start(500);
  
  // Iniciar Mozzi
  startMozzi(16384);
}

// ============================================================
// loop() - Bucle principal
// ============================================================
void loop() {
  audioHook();
}