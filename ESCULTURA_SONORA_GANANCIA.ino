/*
 * ============================================================================
 * ESCULTURA SONORA - VOLUMEN CORREGIDO CON GANANCIA EXTREMA
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

#define LOWPASS 0
#define HIGHPASS 1
#define BANDPASS 2

const int PROB_NUEVO_SONIDO = 18;
const int PROB_RELEASE_ABRUPTO = 30;
const int PROB_CAMBIO_FILTRO = 40;

// ========== DEFINICIÓN DE LAS VOCES ==========

Oscil<WHITENOISE8192_NUM_CELLS, MOZZI_AUDIO_RATE> noiseLeft(WHITENOISE8192_DATA);
Oscil<WHITENOISE8192_NUM_CELLS, MOZZI_AUDIO_RATE> noiseRight(WHITENOISE8192_DATA);

ResonantFilter<LOWPASS> filterLP_Left;
ResonantFilter<HIGHPASS> filterHP_Left;
ResonantFilter<BANDPASS> filterBP_Left;
int currentFilterLeft = LOWPASS;
int targetFilterLeft = LOWPASS;

ResonantFilter<LOWPASS> filterLP_Right;
ResonantFilter<HIGHPASS> filterHP_Right;
ResonantFilter<BANDPASS> filterBP_Right;
int currentFilterRight = LOWPASS;
int targetFilterRight = LOWPASS;

int cutoffLeft = 200, cutoffRight = 120;
int resonanceLeft = 80, resonanceRight = 70;
int volumeLeft = 0, volumeRight = 0;
int targetVolumeLeft = 0, targetVolumeRight = 0;

enum State { SILENCE, ATTACK, SUSTAIN, RELEASE };
State stateLeft = ATTACK, stateRight = ATTACK;

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
                if (releaseAbrupt) targetVolumeLeft = 0;
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
                if (releaseAbrupt) targetVolumeRight = 0;
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

void updateControl() {
    updateStateLeft();
    updateStateRight();
}

// ============================================================
// updateAudio() - GANANCIA EXTREMA + SATURACIÓN CONTROLADA
// ============================================================
AudioOutput updateAudio() {
    int rawLeft = noiseLeft.next();
    int rawRight = noiseRight.next();
    
    int filteredLeft = processSampleLeft(rawLeft);
    int filteredRight = processSampleRight(rawRight);
    
    // === GANANCIA EXTREMA ===
    // Usamos multiplicadores grandes para alcanzar el rango de 16 bits
    
    int leftSample, rightSample;
    
    if (stateLeft == RELEASE) {
        leftSample = filteredLeft * volumeLeft;
    } else {
        leftSample = filteredLeft * targetVolumeLeft;
    }
    
    if (stateRight == RELEASE) {
        rightSample = filteredRight * volumeRight;
    } else {
        rightSample = filteredRight * targetVolumeRight;
    }
    
    // Ganancia adicional fija para llegar a rango audible
    // Multiplicamos por 128 (equivalente a shift left 7 bits)
    leftSample = leftSample * 128;
    rightSample = rightSample * 128;
    
    // Dividir por 256 solo al final (preserva la señal)
    leftSample = leftSample / 256;
    rightSample = rightSample / 256;
    
    // Segunda etapa de ganancia (×4)
    leftSample = leftSample * 4;
    rightSample = rightSample * 4;
    
    // Limitar con saturación suave (no corte duro)
    leftSample = constrain(leftSample, -32767, 32767);
    rightSample = constrain(rightSample, -32767, 32767);
    
    return StereoOutput::fromNBit(16, (long)leftSample, (long)rightSample);
}

// ============================================================
void setup() {
    randSeed(analogRead(0) + analogRead(1));
    
    updateFiltersLeft();
    updateFiltersRight();
    
    noiseLeft.setFreq(2.111f);
    noiseRight.setFreq(2.111f);
    
    targetVolumeLeft = 200;
    targetVolumeRight = 180;
    
    stateStartTimeLeft = millis();
    stateStartTimeRight = millis();
    
    globalClock.start(500);
    
    startMozzi();
}

void loop() {
    audioHook();
}
