/**
 * ============================================================
 *  ESP32 Magic Eye — Test de arranque
 * ============================================================
 *  Prueba básica que:
 *    1. Activa el módulo de alto voltaje (HV) poniendo GPIO4 a HIGH.
 *    2. Genera una señal senoidal continua por el DAC (GPIO25)
 *       para excitar la entrada del módulo VU Magic Eye.
 *
 *  Conexiones:
 *    GPIO 4  → ENABLE del módulo HV  (nivel alto = módulo activo)
 *    GPIO 25 → IN_L del módulo VU    (señal analógica 0–3.3V)
 *    GND     → GND común de todos los módulos
 *
 *  ADVERTENCIA: El módulo HV genera ~250V DC. ¡No toques las
 *  conexiones de alta tensión con el circuito energizado!
 * ============================================================
 */

#include <Arduino.h>
#include <math.h>

// ── Pines ────────────────────────────────────────────────────
#define PIN_HV_ENABLE  4    // GPIO4  → ENABLE del módulo HV
#define PIN_DAC_OUT    25   // GPIO25 → DAC1, señal al módulo VU

// ── Parámetros de la señal ───────────────────────────────────
// Frecuencia de la señal senoidal (Hz)
// Valores entre 1 y 10 Hz hacen que el "ojo" se abra y cierre
// lentamente, perfectos para una primera prueba visual.
const float SIGNAL_FREQ_HZ = 2.0f;

// Amplitud: el DAC del ESP32 va de 0 a 255 (8 bits, 0–3.3V).
// Usamos el rango completo centrado en el medio.
const uint8_t DAC_MID       = 127;   // Punto central (≈ 1.65 V)
const uint8_t DAC_AMPLITUDE = 120;   // Amplitud pico (deja margen)

// Número de pasos por ciclo completo de la senoidal
// (más pasos = onda más suave, pero más carga de CPU)
const int STEPS_PER_CYCLE = 200;

// ── Variables globales ────────────────────────────────────────
// Tabla de seno precalculada para evitar llamadas costosas a sinf()
// en cada iteración del loop.
uint8_t sineTable[STEPS_PER_CYCLE];

// Índice actual en la tabla
int sineIndex = 0;

// Período entre muestras en microsegundos
unsigned long stepPeriodUs = 0;

// Timestamp de la última muestra
unsigned long lastStepUs = 0;

// ────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== ESP32 Magic Eye — Test HV + DAC ===");

  // 1. Configurar y activar el ENABLE del módulo HV
  pinMode(PIN_HV_ENABLE, OUTPUT);
  digitalWrite(PIN_HV_ENABLE, HIGH);
  Serial.println("[OK] HV ENABLE → HIGH (módulo HV activado)");
  Serial.println("     ⚠️  ¡ALTO VOLTAJE ACTIVO! No toques el circuito.");

  // 2. Precalcular la tabla de seno
  //    Valor = MID + AMPLITUDE * sin(2π * i / STEPS)
  for (int i = 0; i < STEPS_PER_CYCLE; i++) {
    float angle = (2.0f * M_PI * i) / STEPS_PER_CYCLE;
    sineTable[i] = (uint8_t)(DAC_MID + DAC_AMPLITUDE * sinf(angle));
  }

  // 3. Calcular el período entre muestras
  //    T_total = 1 / freq  →  T_paso = T_total / STEPS
  stepPeriodUs = (unsigned long)(1000000.0f / (SIGNAL_FREQ_HZ * STEPS_PER_CYCLE));

  Serial.printf("[OK] Señal senoidal: %.1f Hz, %d pasos/ciclo, paso cada %lu µs\n",
                SIGNAL_FREQ_HZ, STEPS_PER_CYCLE, stepPeriodUs);
  Serial.printf("     DAC central: %d (≈%.2fV), amplitud: ±%d\n",
                DAC_MID, DAC_MID * 3.3f / 255.0f, DAC_AMPLITUDE);
  Serial.println("[OK] Generando señal en GPIO25 → módulo VU...");

  lastStepUs = micros();
}

// ────────────────────────────────────────────────────────────
void loop() {
  // Avanzar al siguiente paso de la senoidal cuando toque
  unsigned long now = micros();
  if (now - lastStepUs >= stepPeriodUs) {
    lastStepUs = now;

    // Escribir el valor actual de la tabla en el DAC
    dacWrite(PIN_DAC_OUT, sineTable[sineIndex]);

    // Avanzar al siguiente paso (circular)
    sineIndex = (sineIndex + 1) % STEPS_PER_CYCLE;
  }

  // Aquí en el futuro irá la recepción de datos por Serial USB
  // para el modo VU Meter real.
}
