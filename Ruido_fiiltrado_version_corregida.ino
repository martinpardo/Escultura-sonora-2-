/*
 * ============================================================================
 = ESCULTURA SONORA - RUIDO FILTRADO AUTÓNOMO
 * ============================================================================
 *  ESP32-C3 Super Mini + DAC externo PCM5102 (I2S)
 *  
 *  Basado en:
 *  - Pinout seguro: GPIO5 (BCK), GPIO6 (WS), GPIO7 (DIN)
 *  - Configuración I2S para Mozzi
 *  - Generadores pseudoaleatorios, silencios independientes,
 *    filtros variables (LP/HP/BP) y ataques percusivos.
 * ============================================================================
 */

// ========== 1. CONFIGURACIÓN DE MOZZI (OBLIGATORIO ANTES DE #include <Mozzi.h>) ==========
#include <MozziConfigValues.h>

#define MOZZI_AUDIO_MODE       MOZZI_OUTPUT_I2S_DAC
#define MOZZI_AUDIO_CHANNELS   MOZZI_STEREO

// Pines I2S para ESP32-C3 Super Mini (según guía oficial)
#define MOZZI_I2S_PIN_BCK      5
#define MOZZI_I2S_PIN_WS       6
#define MOZZI_I2S_PIN_DATA     7

#define MOZZI_AUDIO_BITS       16
#define MOZZI_CONTROL_RATE     256

// ========== 2. LIBRERÍAS ==========
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/whitenoise8192_int8.h>
#include <ResonantFilter.h>
#include <EventDelay.h>
#include <mozzi_rand.h>

// ========== 3. CONSTANTES DE COMPORTAMIENTO ==========
#define LOWPASS      0
#define HIGHPASS     1
#define BANDPASS     2

const int PROB_NUEVO_SONIDO      = 18;   // 18% de probabilidad cada ciclo
const int PROB_RELEASE_ABRUPTO   = 30;   // 30% de release abrupto
const int PROB_CAMBIO_FILTRO     = 40;   // 40% de cambiar filtro

// ========== 4. CANAL IZQUIERDO (AGUDOS) ==========
Oscil<WHITENOISE8192_NUM_CELLS, MOZZI_AUDIO_RATE> noiseLeft(WHITENOISE8192_DATA);
ResonantFilter<LOWPASS>  filterLP_Left;
ResonantFilter<HIGHPASS> filterHP_Left;
ResonantFilter<BANDPASS> filterBP_Left;
int currentFilterLeft = LOWPASS;
int targetFilterLeft  = LOWPASS;

// ========== 5. CANAL DERECHO (GRAVES) ==========
Oscil<WHITENOISE8192_NUM_CELLS, MOZZI_AUDIO_RATE> noiseRight(WHITENOISE8192_DATA);
ResonantFilter<LOWPASS>  filterLP_Right;
ResonantFilter<HIGHPASS> filterHP_Right;
ResonantFilter<BANDPASS> filterBP_Right;
int currentFilterRight = LOWPASS;
int targetFilterRight  = LOWPASS;

// ========== 6. PARÁMETROS COMPARTIDOS ==========
int cutoffLeft     = 200, cutoffRight     = 120;
int resonanceLeft  = 80,  resonanceRight  = 70;
int volumeLeft     = 0,   volumeRight     = 0;
int targetVolumeLeft  = 0, targetVolumeRight  = 0;

// ========== 7. MÁQUINA DE ESTADOS ==========
enum State { SILENCE, ATTACK, SUSTAIN, RELEASE };
State stateLeft  = ATTACK;   // ATTACK para sonido inmediato
State stateRight = ATTACK;

unsigned long stateStartTimeLeft  = 0, stateStartTimeRight  = 0;
int attackDuration   = 100;
int sustainDuration  = 3000;
int releaseDuration  = 500;
bool releaseAbrupt   = false;

unsigned long lastParamChangeLeft  = 0, lastParamChangeRight  = 0;
int paramChangeInterval = 2000;

unsigned long lastFilterChangeLeft  = 0, lastFilterChangeRight  = 0;
int filterChangeInterval = 3000;

EventDelay globalClock;

// ========== 8. FUNCIONES DE ACTUALIZACIÓN DE FILTROS ==========
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

// ========== 9. PROCESAMIENTO DE SEÑAL POR FILTRO ACTIVO ==========
int processSampleLeft(int raw) {
    switch(currentFilterLeft) {
        case LOWPASS:  return filterLP_Left.next(raw);
        case HIGHPASS: return filterHP_Left.next(raw);
        case BANDPASS: return filterBP_Left.next(raw);
        default:       return filterLP_Left.next(raw);
    }
}

int processSampleRight(int raw) {
    switch(currentFilterRight) {
        case LOWPASS:  return filterLP_Right.next(raw);
        case HIGHPASS: return filterHP_Right.next(raw);
        case BANDPASS: return filterBP_Right.next(raw);
        default:       return filterLP_Right.next(raw);
    }
}

// ========== 10. INICIO DE ATAQUE (CANAL IZQUIERDO) ==========
void startAttackLeft() {
    stateLeft = ATTACK;
    stateStartTimeLeft = millis();
    targetVolumeLeft = rand(200, 255);
    cutoffLeft       = rand(160, 240);
    resonanceLeft    = rand(40, 120);
    targetFilterLeft = rand(0, 3);
    updateFiltersLeft();
    lastParamChangeLeft  = millis();
    lastFilterChangeLeft = millis();
}

// ========== 11. INICIO DE ATAQUE (CANAL DERECHO) ==========
void startAttackRight() {
    stateRight = ATTACK;
    stateStartTimeRight = millis();
    targetVolumeRight = rand(180, 240);
    cutoffRight       = rand(80, 160);
    resonanceRight    = rand(40, 100);
    targetFilterRight = rand(0, 3);
    updateFiltersRight();
    lastParamChangeRight  = millis();
    lastFilterChangeRight = millis();
}

// ========== 12. MÁQUINA DE ESTADOS - IZQUIERDO ==========
void updateStateLeft() {
    unsigned long now = millis();
    unsigned long elapsed = now - stateStartTimeLeft;

    switch(stateLeft) {
        case SILENCE:
            if (globalClock.ready() && rand(1, 101) < PROB_NUEVO_SONIDO)
                startAttackLeft();
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
                cutoffLeft    = rand(120, 240);
                resonanceLeft = rand(40, 150);
                updateFiltersLeft();
                lastParamChangeLeft = now;
                paramChangeInterval = rand(800, 5000);
            }

            if (now - lastFilterChangeLeft > (unsigned long)filterChangeInterval) {
                if (rand(1, 101) < PROB_CAMBIO_FILTRO)
                    targetFilterLeft = rand(0, 3);
                lastFilterChangeLeft = now;
                filterChangeInterval = rand(2000, 8000);
            }

            if (currentFilterLeft != targetFilterLeft)
                currentFilterLeft = targetFilterLeft;

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

// ========== 13. MÁQUINA DE ESTADOS - DERECHO ==========
void updateStateRight() {
    unsigned long now = millis();
    unsigned long elapsed = now - stateStartTimeRight;

    switch(stateRight) {
        case SILENCE:
            if (globalClock.ready() && rand(1, 101) < PROB_NUEVO_SONIDO)
                startAttackRight();
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
                cutoffRight    = rand(50, 180);
                resonanceRight = rand(40, 130);
                updateFiltersRight();
                lastParamChangeRight = now;
                paramChangeInterval = rand(800, 5000);
            }

            if (now - lastFilterChangeRight > (unsigned long)filterChangeInterval) {
                if (rand(1, 101) < PROB_CAMBIO_FILTRO)
                    targetFilterRight = rand(0, 3);
                lastFilterChangeRight = now;
                filterChangeInterval = rand(2000, 8000);
            }

            if (currentFilterRight != targetFilterRight)
                currentFilterRight = targetFilterRight;

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

// ========== 14. CONTROL PRINCIPAL ==========
void updateControl() {
    updateStateLeft();
    updateStateRight();
}

// ========== 15. GENERACIÓN DE AUDIO (SALIDA ESTÉREO) ==========
AudioOutput updateAudio() {
    int rawLeft  = noiseLeft.next();
    int rawRight = noiseRight.next();

    int filteredLeft  = processSampleLeft(rawLeft);
    int filteredRight = processSampleRight(rawRight);

    int leftSample, rightSample;

    // Aplicamos volumen según estado (ATTACK/SUSTAIN o RELEASE)
    if (stateLeft == RELEASE)
        leftSample = filteredLeft * volumeLeft;
    else
        leftSample = filteredLeft * targetVolumeLeft;

    if (stateRight == RELEASE)
        rightSample = filteredRight * volumeRight;
    else
        rightSample = filteredRight * targetVolumeRight;

    // Ganancia para alcanzar rango audible (≈ +30dB sobre el ruido base)
    leftSample  = (leftSample * 128) / 256;
    leftSample  = leftSample * 8;
    rightSample = (rightSample * 128) / 256;
    rightSample = rightSample * 8;

    leftSample  = constrain(leftSample,  -32767, 32767);
    rightSample = constrain(rightSample, -32767, 32767);

    return StereoOutput::fromNBit(MOZZI_AUDIO_BITS, (long)leftSample, (long)rightSample);
}

// ========== 16. CONFIGURACIÓN INICIAL ==========
void setup() {
    randSeed(analogRead(0) + analogRead(1));

    updateFiltersLeft();
    updateFiltersRight();

    noiseLeft.setFreq(2.111f);
    noiseRight.setFreq(2.111f);

    targetVolumeLeft  = 200;
    targetVolumeRight = 180;

    stateStartTimeLeft  = millis();
    stateStartTimeRight = millis();

    globalClock.start(500);

    startMozzi(MOZZI_CONTROL_RATE);
}

// ========== 17. BUCLE PRINCIPAL ==========
void loop() {
    audioHook();
}