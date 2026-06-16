/**
 * ============================================================
 *  ESP32 Magic Eye — Test de amplitud variable (sin serie)
 * ============================================================
 *
 *  Demuestra que podemos controlar el nivel del tubo sin
 *  modificar el circuito, usando una onda cuadrada a 1 Hz
 *  donde el nivel ALTO varía progresivamente.
 *
 *  El firmware hace un "barrido de amplitud":
 *    - Genera una onda cuadrada fija a 1 Hz
 *    - El nivel HIGH sube de 0 a 255 en ~8 s, luego baja de
 *      255 a 0 en ~8 s, y repite indefinidamente.
 *    - El nivel LOW siempre es 0.
 *
 *  Si el tubo responde, se debería ver cómo el ojo se abre
 *  progresivamente conforme sube el nivel, y se cierra al bajar.
 *  Esto confirma que podemos usar esta técnica para el VU meter.
 *
 *  LED:
 *    - Durante el semiciclo HIGH: LED encendido
 *    - Durante el semiciclo LOW:  LED apagado
 *    (así puedes ver el ritmo de 1 Hz)
 *
 *  Conexiones:
 *    GPIO 4  → ENABLE módulo HV  (HIGH = activo)
 *    GPIO 25 → IN_L módulo VU
 *    GPIO 2  → LED integrado
 *    GND     → GND común
 * ============================================================
 */

#include <Arduino.h>

// ── Pines ────────────────────────────────────────────────────
#define PIN_HV_ENABLE  4
#define PIN_DAC_OUT   25
#define PIN_LED        2

// ── Parámetros ────────────────────────────────────────────────
// Frecuencia de la onda cuadrada (Hz)
// Debe estar entre 1-5 Hz para que el detector de pico del módulo
// tenga tiempo de descargarse entre ciclos.
static const float  CARRIER_HZ   = 1.5f;

// Semiperíodo en microsegundos
static const uint32_t HALF_PERIOD_US = (uint32_t)(500000.0f / CARRIER_HZ);

// Cuántos ciclos completos de onda cuadrada por "paso" de amplitud
// Con CARRIER=1.5Hz un ciclo dura ~667ms. 2 ciclos = ~1.33s por paso.
// 16 pasos × 16 valores = 256 valores → barrido completo en ~21s
static const int CYCLES_PER_STEP = 2;   // ciclos antes de cambiar amplitud
static const int AMPLITUDE_STEP  = 16;  // cuánto sube/baja el nivel HIGH por paso

// ── Estado ────────────────────────────────────────────────────
bool     squareHigh    = false;   // mitad alta o baja del ciclo
uint32_t lastToggleUs  = 0;       // µs del último toggle

int      targetLevel   = 0;       // nivel HIGH actual (0-255)
int      levelDir      = 1;       // 1=subiendo, -1=bajando
int      cycleCount    = 0;       // ciclos completados en este paso

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // Latch HIGH antes de OUTPUT para evitar glitch a LOW en boot
  digitalWrite(PIN_HV_ENABLE, HIGH);
  pinMode(PIN_HV_ENABLE, OUTPUT);

  dacWrite(PIN_DAC_OUT, 0);
  lastToggleUs = micros();
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  uint32_t nowUs = micros();

  if (nowUs - lastToggleUs >= HALF_PERIOD_US) {
    lastToggleUs = nowUs;
    squareHigh   = !squareHigh;

    if (squareHigh) {
      // ── Semiciclo HIGH: sacar el nivel actual ───────────────
      dacWrite(PIN_DAC_OUT, (uint8_t)targetLevel);
      digitalWrite(PIN_LED, HIGH);
    } else {
      // ── Semiciclo LOW: siempre 0V ───────────────────────────
      dacWrite(PIN_DAC_OUT, 0);
      digitalWrite(PIN_LED, LOW);

      // Contar ciclo completo (se cuenta al bajar)
      cycleCount++;
      if (cycleCount >= CYCLES_PER_STEP) {
        cycleCount = 0;

        // Avanzar amplitud
        targetLevel += levelDir * AMPLITUDE_STEP;

        if (targetLevel >= 255) {
          targetLevel = 255;
          levelDir    = -1;   // empezar a bajar
        }
        if (targetLevel <= 0) {
          targetLevel = 0;
          levelDir    = 1;    // empezar a subir
        }
      }
    }
  }
}
