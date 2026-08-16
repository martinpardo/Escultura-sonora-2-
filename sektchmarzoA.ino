/*
 * ============================================================================
 * INSTALACIÓN SONORA - 2 CANALES ESTÉREO (VERSIÓN FINAL CORREGIDA)
 * Basado en Mozzi para Arduino Uno/Nano
 * 
 * Hardware:
 * - 1 Arduino Uno/Nano
 * - 2 parlantes de 8" (uno para agudos, otro para graves)
 * 
 * Conexiones:
 *   Pin 9  -> Filtro RC -> Parlante IZQUIERDO (AGUDOS)
 *   Pin 10 -> Filtro RC -> Parlante DERECHO (GRAVES)
 * ============================================================================
 */

// ========== CONFIGURACIÓN ESTÉREO - DEBE IR ANTES DE MOZZI ==========
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
// ======================================================================

#include <Mozzi.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>
#include <tables/saw2048_int8.h>
#include <tables/whitenoise8192_int8.h>
#include <EventDelay.h>
#include <mozzi_rand.h>

// --- Definición de Osciladores ---
// Canal IZQUIERDO (AGUDOS)
Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> oscAgudos(SIN2048_DATA);
Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> oscAgudosArmonicos(SAW2048_DATA);

// Canal DERECHO (GRAVES)
Oscil <SIN2048_NUM_CELLS, AUDIO_RATE> oscGraves(SIN2048_DATA);
Oscil <WHITENOISE8192_NUM_CELLS, AUDIO_RATE> oscGolpes(WHITENOISE8192_DATA);

// --- Variables de Control ---
int frecuenciaAgudos = 880;
int frecuenciaGraves = 110;
int volumenAgudos = 200;
int volumenGraves = 180;
bool sonando = true;

// --- Variables para el control de silencios y percusión ---
EventDelay controlSilencios;
EventDelay controlTimbre;
bool golpeActivo = false;
unsigned long tiempoGolpe = 0;
int duracionGolpe = 0;

// --- Variables para aleatoriedad ---
unsigned long semilla = 0;

// ============================================================
// updateControl() - Decisiones compositivas (baja frecuencia)
// ============================================================
void updateControl() {
  // 1. CONTROL DE SILENCIOS (alterna entre sonido y silencio)
  if (controlSilencios.ready()) {
    // 30% de probabilidad de cambiar estado
    if (rand(1, 101) < 30) {
      sonando = !sonando;
      
      if (sonando) {
        // Bloque de sonido: dura entre 2 y 10 segundos
        controlSilencios.start(rand(2000, 10000));
        
        // Nuevas frecuencias para este bloque
        frecuenciaAgudos = rand(600, 8000);
        frecuenciaGraves = rand(60, 600);
        
        // Probabilidad de tener un golpe percusivo (40%)
        if (rand(1, 101) < 40) {
          duracionGolpe = rand(80, 400);
          golpeActivo = true;
          tiempoGolpe = millis();
        }
        
      } else {
        // Bloque de silencio: dura entre 3 y 12 segundos
        controlSilencios.start(rand(1000, 5000));
        duracionGolpe = 0;
        golpeActivo = false;
      }
    }
  }
  
  // 2. CONTROL DE TIMBRE (cambios dentro del bloque de sonido)
  if (sonando && controlTimbre.ready()) {
    // Pequeñas variaciones de frecuencia
    frecuenciaAgudos = constrain(frecuenciaAgudos + rand(-80, 81), 400, 4000);
    frecuenciaGraves = constrain(frecuenciaGraves + rand(-40, 41), 50, 500);
    
    // Variaciones de volumen (para crear dinámicas)
    volumenAgudos = rand(150, 255);
    volumenGraves = rand(150, 255);
    
    // Programar próximo cambio (0.5 a 4 segundos)
    controlTimbre.start(rand(500, 4000));
  }
  
  // Aplicar frecuencias a los osciladores
  oscAgudos.setFreq(frecuenciaAgudos);
  // ✅ CORREGIDO: usar float explícito
  oscAgudosArmonicos.setFreq((float)frecuenciaAgudos * 2.01f);
  oscGraves.setFreq(frecuenciaGraves);
}

// ============================================================
// updateAudio() - Generación de audio (alta frecuencia)
// ============================================================
AudioOutput_t updateAudio() {
  int muestraAgudos = 0;
  int muestraGraves = 0;
  
  if (sonando) {
    // --- CANAL IZQUIERDO (AGUDOS) ---
    // Mezcla de seno y diente de sierra para textura
    int seno = oscAgudos.next();
    int sierra = oscAgudosArmonicos.next();
    muestraAgudos = (seno + sierra) / 2;
    muestraAgudos = (muestraAgudos * volumenAgudos) / 256;
    
    // --- CANAL DERECHO (GRAVES) ---
    // Onda seno base
    muestraGraves = oscGraves.next();
    
    // Añadir percusión (golpe) si está activo
    if (golpeActivo) {
      unsigned long tiempoTranscurrido = millis() - tiempoGolpe;
      
      if (tiempoTranscurrido < (unsigned long)duracionGolpe) {
        // Decaimiento lineal del golpe
        int intensidad = 255 - (tiempoTranscurrido * 255 / duracionGolpe);
        int ruido = oscGolpes.next();
        int golpe = (ruido * intensidad) / 128;
        
        // Mezclar golpe con la señal base
        muestraGraves = (muestraGraves + golpe) / 2;
      } else {
        golpeActivo = false; // Fin del golpe
      }
    }
    
    // Aplicar volumen al canal de graves
    muestraGraves = (muestraGraves * volumenGraves) / 256;
  }
  
  // Devolver señal estéreo
  return StereoOutput::from16Bit(muestraAgudos * 256, muestraGraves * 256);
}

// ============================================================
// setup() - Configuración inicial
// ============================================================
void setup() {
  // Semilla para números aleatorios desde pin analógico
  semilla = analogRead(A0);
  randSeed(semilla);
  
  // Configurar frecuencias iniciales
  oscAgudos.setFreq(880);
  // ✅ CORREGIDO: usar float explícito
  oscAgudosArmonicos.setFreq(1760.0f);
  oscGraves.setFreq(110);
  
  // Inicializar temporizadores
  controlSilencios.start(1000);  // Primera decisión a 1 segundo
  controlTimbre.start(2000);      // Primer cambio de timbre a 2 segundos
  
  // Iniciar Mozzi con tasa de audio de 16384 Hz (estable)
  startMozzi(16384);
}

// ============================================================
// loop() - Bucle principal
// ============================================================
void loop() {
  audioHook();  // Mantiene Mozzi funcionando
}