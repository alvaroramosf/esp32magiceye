/**
 * ESP32 Magic Eye - CPU Monitor Firmware
 *
 * Connects to WiFi and listens for UDP packets from the host PC.
 * Each packet carries a single byte (0-255) representing the CPU load.
 * The firmware drives the Magic Eye VU module via DAC using a low-frequency
 * square wave whose amplitude is proportional to the received value.
 *
 * Hardware pinout:
 *   GPIO 25 - DAC1  -> Magic Eye VU module signal input (IN_L)
 *   GPIO 4  - OUTPUT -> High-voltage module enable (active HIGH)
 *
 * UDP protocol:
 *   Single-byte packet, value 0-255  (maps to 0-100 % CPU load linearly)
 *   Default port: 4210
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "secrets.h"

// User configuration
// Edit secrets.h to set your WiFi credentials.

#define UDP_PORT 4210 // UDP port to listen on

// Hardware pins

#define PIN_DAC 25      // DAC1 - analog signal to VU module
#define PIN_HV_ENABLE 4 // High-voltage module enable (active HIGH)

// Square wave parameters
// The VU module uses an AC-coupling cap + peak detector, so it only responds
// to amplitude changes. A ~1.5 Hz square wave lets the detector charge and
// discharge cleanly, giving the classic "bouncing needle" VU effect.

#define WAVE_HALF_PERIOD_MS 333 // half-period = 333 ms -> full period ~1.5 Hz

// Standby
// HV module is shut down if no UDP data is received for this many ms.
// It wakes up automatically when data resumes.

#define STANDBY_TIMEOUT_MS 5000

// Globals

WiFiUDP udp;

uint8_t g_level = 0;                // current DAC level (0-255)
bool g_hv_active = false;           // true while HV module is enabled
unsigned long g_last_packet_ms = 0; // millis() of the last valid packet

// Forward declarations

void connect_wifi();
void set_hv(bool enable);
void poll_udp();
void check_standby();
void drive_square_wave_cycle(uint8_t level);

// Setup

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== ESP32 Magic Eye - CPU Monitor ==="));

  // Keep HV off during boot
  pinMode(PIN_HV_ENABLE, OUTPUT);
  set_hv(false);
  dacWrite(PIN_DAC, 0);

  connect_wifi();

  udp.begin(UDP_PORT);
  Serial.printf("Listening on UDP port %d\n", UDP_PORT);

  // Enable HV once network is ready
  set_hv(true);

  // Arm standby watchdog from now so we don't immediately time out
  g_last_packet_ms = millis();
}

// Main loop

void loop() {
  poll_udp();
  check_standby();

  if (g_hv_active) {
    drive_square_wave_cycle(g_level);
  } else {
    // Standby: keep DAC silent and yield CPU
    dacWrite(PIN_DAC, 0);
    poll_udp(); // still watch for incoming data to wake up
    delay(50);
  }
}

// WiFi

void connect_wifi() {
  Serial.printf("Connecting to \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.printf("\nConnected - IP: %s\n", WiFi.localIP().toString().c_str());
}

// HV control

void set_hv(bool enable) {
  digitalWrite(PIN_HV_ENABLE, enable ? HIGH : LOW);
  g_hv_active = enable;
  Serial.printf("HV module: %s\n", enable ? "ON" : "OFF (standby)");
}

// UDP reception

/**
 * Non-blocking UDP poll. If a packet is available, reads the first byte as
 * the new DAC level and refreshes the standby watchdog.
 */
void poll_udp() {
  int size = udp.parsePacket();
  if (size <= 0)
    return;

  uint8_t buf[4];
  int len = udp.read(buf, sizeof(buf));
  if (len <= 0)
    return;

  g_level = buf[0];
  g_last_packet_ms = millis();

  if (!g_hv_active) {
    Serial.println("Data received - waking from standby.");
    set_hv(true);
  }
}

// Standby watchdog

void check_standby() {
  if (g_hv_active && (millis() - g_last_packet_ms > STANDBY_TIMEOUT_MS)) {
    Serial.println("No data for 5 s - entering standby.");
    set_hv(false);
    g_level = 0;
  }
}

// DAC / square wave

/**
 * Generates one full square wave cycle:
 *   HIGH half -> DAC = level   (peak detector charges to this amplitude)
 *   LOW  half -> DAC = 0       (peak detector discharges -> eye closes
 * partially)
 *
 * poll_udp() is called every 10 ms within each half so the firmware stays
 * responsive to new level values even during the wave generation.
 */
void drive_square_wave_cycle(uint8_t level) {
  // --- HIGH half ---
  dacWrite(PIN_DAC, level);
  unsigned long t = millis();
  while (millis() - t < WAVE_HALF_PERIOD_MS) {
    poll_udp();
    delay(10);
  }

  // --- LOW half ---
  dacWrite(PIN_DAC, 0);
  t = millis();
  while (millis() - t < WAVE_HALF_PERIOD_MS) {
    poll_udp();
    delay(10);
  }
}
