/*
 * ============================================================================
 * ESCULTURA SONORA_VERSIÓN_ESP32
 * Basado en el ejemplo funcional de Mozzi
 * ============================================================================
 */

#include <MozziConfigValues.h>
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO

#include <Mozzi.h>
#include <Oscil.h>
#include <tables/whitenoise8192_int8.h>
#include <ResonantFilter.h>
#include <EventDelay.h>
#include <mozzi_rand.h>

// ========== TIPOS DE FILTRO ==========
#define LOWPASS 0
#define HIGHPASS 1
#define BANDPASS 2

// ========== PARÁMETROS DE COMPORTAMIENTO ==========
const int PROB_NUEVO_SONIDO = 18;
const int PROB_RELEASE_ABRUPTO = 30;
const int PROB_ATAQUE_PERCUSIVO = 35;
const int PROB_CAMBIO_FILTRO = 40;

// ========== DEFINICIÓN DE LAS VOCES ==========

// --- Canal Izquierdo (Agudos) ---
Oscil<WHITENOISE8192_NUM_CELLS, MOZZI_AUDIO_RATE> noiseLeft(WHITENOISE8192_DATA);
ResonantFilter<LOWPASS> filterLP_Left;
ResonantFilter<HIGHPASS> filterHP_Left;
ResonantFilter<BANDPASS> filterBP_Left;
int currentFilterLeft = LOWPASS;
int targetFilterLeft = LOWPASS;

// --- Canal Derecho (Graves) ---
Oscil<WHITENOISE8192_NUM_CELLS, MOZZI_AUDIO_RATE> noiseRight(WHITENOISE8192_DATA);
ResonantFilter<LOWPASS> filterLP_Right;
ResonantFilter<HIGHPASS> filterHP_Right;
ResonantFilter<BANDPASS> filterBP_Right;
int currentFilterRight = LOWPASS;
int targetFilterRight = LOWPASS;

// --- Parámetros compartidos ---
uint8_t cutoffLeft = 200, cutoffRight = 120;
uint8_t resonanceLeft = 80, resonanceRight = 70;
uint8_t volumeLeft = 0, volumeRight = 0;
uint8_t targetVolumeLeft = 0, targetVolumeRight = 0;

// --- Máquina de estados ---
enum State { SILENCE, ATTACK, SUSTAIN, RELEASE };
State stateLeft = SILENCE, stateRight = SILENCE;

unsigned long stateStartTimeLeft = 0, stateStartTimeRight = 0;
int attackDuration = 100;
int sustainDuration = 3000;
int releaseDuration = 500;
bool releaseAbrupt = false;

unsigned long lastParamChangeLeft = 0, lastParamChangeRight = 0;
int paramChangeInterval = 2000;

unsigned long lastFilterChangeLeft = 0, lastFilterChangeRight = 0;
int filterChangeInterval = 3000;

EventDelay globalClock;

// ============================================================
// Inicializar filtros
// ============================================================
void updateFiltersLeft() {
    filterLP_Left.setCutoffFreqAndResonance(cutoffLeft, resonanceLeft);
    filterHP_Left.setCutoffFreqAndResonance(cutoffLeft, resonanceLeft);
    filterBP_Left.setCutoffFreqAndResonance(cutoffLeft, resonanceLeft);
}

void updateFiltersRight() {
    filterLP_Right.setCutoffFreqAndResonance(cutoffRight, resonanceRight);
    filterHP_Right.setCutoffFreqAndResonance(cutoffRight, resonanceRight);
    filterBP_Right.setCutoffFreqAndResonance(cutoffRight, resonanceRight);
}

// ============================================================
// Procesar muestra con filtro activo
// ============================================================
int processSampleLeft(int raw) {
    switch(currentFilterLeft) {
        case LOWPASS: return filterLP_Left.next(raw);
        case HIGHPASS: return filterHP_Left.next(raw);
        case BANDPASS: return filterBP_Left.next(raw);
        default: return filterLP_Left.next(raw);
    }
}

int processSampleRight(int raw) {
    switch(currentFilterRight) {
        case LOWPASS: return filterLP_Right.next(raw);
        case HIGHPASS: return filterHP_Right.next(raw);
        case BANDPASS: return filterBP_Right.next(raw);
        default: return filterLP_Right.next(raw);
    }
}

// ============================================================
// Iniciar ataque (Canal Izquierdo)
// ============================================================
void startAttackLeft() {
    stateLeft = ATTACK;
    stateStartTimeLeft = millis();
    targetVolumeLeft = rand(200, 255);
    cutoffLeft = rand(160, 240);
    resonanceLeft = rand(40, 120);
    targetFilterLeft = rand(0, 3);
    updateFiltersLeft();
    lastParamChangeLeft = millis();
    lastFilterChangeLeft = millis();
}

// ============================================================
// Iniciar ataque (Canal Derecho)
// ============================================================
void startAttackRight() {
    stateRight = ATTACK;
    stateStartTimeRight = millis();
    targetVolumeRight = rand(180, 240);
    cutoffRight = rand(80, 160);
    resonanceRight = rand(40, 100);
    targetFilterRight = rand(0, 3);
    updateFiltersRight();
    lastParamChangeRight = millis();
    lastFilterChangeRight = millis();
}

// ============================================================
// Actualizar estado del canal izquierdo
// ============================================================
void updateStateLeft() {
    unsigned long now = millis();
    unsigned long elapsed = now - stateStartTimeLeft;
    
    switch(stateLeft) {
        case SILENCE:
            if (globalClock.ready() && rand(1, 101) < PROB_NUEVO_SONIDO) {
                startAttackLeft();
            }
            break;
            
        case ATTACK:
            if (elapsed < (unsigned long)attackDuration) {
                float t = (float)elapsed / attackDuration;
                volumeLeft = targetVolumeLeft * t;
                if (volumeLeft < targetVolumeLeft) volumeLeft++;
            } else {
                stateLeft = SUSTAIN;
                stateStartTimeLeft = now;
                volumeLeft = targetVolumeLeft;
            }
            break;
            
        case SUSTAIN:
            if (now - lastParamChangeLeft > (unsigned long)paramChangeInterval) {
                cutoffLeft = rand(120, 240);
                resonanceLeft = rand(40, 150);
                updateFiltersLeft();
                lastParamChangeLeft = now;
                paramChangeInterval = rand(800, 5000);
            }
            
            if (now - lastFilterChangeLeft > (unsigned long)filterChangeInterval) {
                if (rand(1, 101) < PROB_CAMBIO_FILTRO) {
                    targetFilterLeft = rand(0, 3);
                }
                lastFilterChangeLeft = now;
                filterChangeInterval = rand(2000, 8000);
            }
            
            if (currentFilterLeft != targetFilterLeft) {
                currentFilterLeft = targetFilterLeft;
            }
            
            if (elapsed > (unsigned long)sustainDuration) {
                stateLeft = RELEASE;
                stateStartTimeLeft = now;
                releaseAbrupt = (rand(1, 101) < PROB_RELEASE_ABRUPTO);
                releaseDuration = releaseAbrupt ? rand(10, 100) : rand(400, 2000);
                if (releaseAbrupt) {
                    targetVolumeLeft = 0;
                }
            }
            break;
            
        case RELEASE:
            if (elapsed < (unsigned long)releaseDuration) {
                if (releaseAbrupt) {
                    if (volumeLeft > 0) volumeLeft -= 4;
                } else {
                    if (volumeLeft > 0) volumeLeft--;
                }
            } else {
                stateLeft = SILENCE;
                volumeLeft = 0;
                targetVolumeLeft = 0;
                sustainDuration = rand(2000, 8000);
            }
            break;
    }
}

// ============================================================
// Actualizar estado del canal derecho
// ============================================================
void updateStateRight() {
    unsigned long now = millis();
    unsigned long elapsed = now - stateStartTimeRight;
    
    switch(stateRight) {
        case SILENCE:
            if (globalClock.ready() && rand(1, 101) < PROB_NUEVO_SONIDO) {
                startAttackRight();
            }
            break;
            
        case ATTACK:
            if (elapsed < (unsigned long)attackDuration) {
                float t = (float)elapsed / attackDuration;
                volumeRight = targetVolumeRight * t;
                if (volumeRight < targetVolumeRight) volumeRight++;
            } else {
                stateRight = SUSTAIN;
                stateStartTimeRight = now;
                volumeRight = targetVolumeRight;
            }
            break;
            
        case SUSTAIN:
            if (now - lastParamChangeRight > (unsigned long)paramChangeInterval) {
                cutoffRight = rand(50, 180);
                resonanceRight = rand(40, 130);
                updateFiltersRight();
                lastParamChangeRight = now;
                paramChangeInterval = rand(800, 5000);
            }
            
            if (now - lastFilterChangeRight > (unsigned long)filterChangeInterval) {
                if (rand(1, 101) < PROB_CAMBIO_FILTRO) {
                    targetFilterRight = rand(0, 3);
                }
                lastFilterChangeRight = now;
                filterChangeInterval = rand(2000, 8000);
            }
            
            if (currentFilterRight != targetFilterRight) {
                currentFilterRight = targetFilterRight;
            }
            
            if (elapsed > (unsigned long)sustainDuration) {
                stateRight = RELEASE;
                stateStartTimeRight = now;
                releaseAbrupt = (rand(1, 101) < PROB_RELEASE_ABRUPTO);
                releaseDuration = releaseAbrupt ? rand(10, 100) : rand(400, 2000);
                if (releaseAbrupt) {
                    targetVolumeRight = 0;
                }
            }
            break;
            
        case RELEASE:
            if (elapsed < (unsigned long)releaseDuration) {
                if (releaseAbrupt) {
                    if (volumeRight > 0) volumeRight -= 4;
                } else {
                    if (volumeRight > 0) volumeRight--;
                }
            } else {
                stateRight = SILENCE;
                volumeRight = 0;
                targetVolumeRight = 0;
                sustainDuration = rand(2000, 8000);
            }
            break;
    }
}

// ============================================================
// updateControl()
// ============================================================
void updateControl() {
    updateStateLeft();
    updateStateRight();
}

// ============================================================
// updateAudio() - VERSIÓN CON GANANCIA CORREGIDA
// ============================================================
AudioOutput updateAudio() {
    // Obtener ruido crudo
    int rawLeft = noiseLeft.next();
    int rawRight = noiseRight.next();
    
    // Aplicar filtros
    int filteredLeft = processSampleLeft(rawLeft);
    int filteredRight = processSampleRight(rawRight);
    
    // Aplicar volumen (0-255)
    int leftSample = (filteredLeft * volumeLeft) / 256;
    int rightSample = (filteredRight * volumeRight) / 256;
    
    // ========== GANANCIA EXTREMA ==========
    // Multiplicar por 16 para aumentar volumen significativamente
    leftSample = leftSample * 16;
    rightSample = rightSample * 16;
    
    // Aplicar ganancia adicional (300%)
    leftSample = (leftSample * 300) / 100;
    rightSample = (rightSample * 300) / 100;
    
    // Limitar al rango de 16 bits para evitar distorsión dura
    leftSample = constrain(leftSample, -32768, 32767);
    rightSample = constrain(rightSample, -32768, 32767);
    
    // Usar el mismo formato que el sketch funcional (24 bits)
    return StereoOutput::fromNBit(24, (long)leftSample, (long)rightSample);
}

// ============================================================
// setup()
// ============================================================
void setup() {
    randSeed(analogRead(0) + analogRead(1));
    
    // Inicializar filtros
    updateFiltersLeft();
    updateFiltersRight();
    
    // Configurar frecuencia del ruido (como en el sketch funcional)
    noiseLeft.setFreq(2.111f);
    noiseRight.setFreq(2.111f);
    
    globalClock.start(500);
    
    startMozzi();
}

// ============================================================
// loop()
// ============================================================
void loop() {
    audioHook();
}
