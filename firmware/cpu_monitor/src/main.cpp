#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "secrets.h"

#define UDP_PORT 4210

// Hardware pins
#define PIN_DAC 25      // DAC1 - analog signal to VU module
#define PIN_HV_ENABLE 4 // High-voltage module enable (active HIGH)

// 2 ms half-period -> 4 ms full period -> 250 Hz square wave.
// This is an audio-frequency tone that the VU meter's peak detector 
// will rectify and smooth into a stable DC voltage without any visible flickering.
#define DAC_TOGGLE_INTERVAL_MICROS 2000
#define STANDBY_TIMEOUT_MS 5000

WiFiUDP udp;

uint8_t g_level = 0;                // current DAC level (0-255)
bool g_hv_active = false;           // true while HV module is enabled
unsigned long g_last_packet_ms = 0; // millis() of the last valid packet
unsigned long g_last_status_ms = 0; // millis() of the last status print

// DAC signal variables
unsigned long g_last_toggle_micros = 0;
bool g_dac_state = false;

void set_hv(bool enable) {
  digitalWrite(PIN_HV_ENABLE, enable ? HIGH : LOW);
  g_hv_active = enable;
  Serial.printf("[STATUS] HV module state: %s\n", enable ? "ON" : "OFF (standby)");
}

void connect_wifi() {
  Serial.printf("[WIFI] Connecting to SSID: \"%s\"\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);

#ifdef USE_STATIC_IP
  IPAddress local_IP(STATIC_IP);
  IPAddress gateway(GATEWAY);
  IPAddress subnet(SUBNET);
#ifdef PRIMARY_DNS
  IPAddress primaryDNS(PRIMARY_DNS);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("[WIFI] Static IP Configuration Failed!");
  }
#else
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("[WIFI] Static IP Configuration Failed!");
  }
#endif
#endif

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Connected! IP address: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("\n[WIFI] Connection failed. Status code: %d\n", WiFi.status());
  }
}

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
    Serial.printf("[UDP] Packet received (level=%d) - waking up from standby.\n", g_level);
    Serial.println("[SYSTEM] WARNING: Enabling High-Voltage booster now. If the ESP32 resets instantly, you need a stronger power supply (Brownout)!");
    set_hv(true);
  }
}

void check_standby() {
  if (g_hv_active && (millis() - g_last_packet_ms > STANDBY_TIMEOUT_MS)) {
    Serial.println("[STANDBY] No UDP data received for 5 seconds. Entering standby.");
    set_hv(false);
    g_level = 0;
    dacWrite(PIN_DAC, 0);
  }
}

// Non-blocking high-frequency square wave generator for the DAC.
// Generates a 250 Hz signal with peak amplitude = g_level.
void drive_dac_signal() {
  if (!g_hv_active) {
    dacWrite(PIN_DAC, 0);
    return;
  }

  unsigned long current_micros = micros();
  if (current_micros - g_last_toggle_micros >= DAC_TOGGLE_INTERVAL_MICROS) {
    g_last_toggle_micros = current_micros;
    g_dac_state = !g_dac_state;
    dacWrite(PIN_DAC, g_dac_state ? g_level : 0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000); // Allow serial monitor to open and stabilize
  Serial.println("\n==============================================");
  Serial.println("[SYSTEM] ESP32 Magic Eye - CPU Monitor Starting");
  Serial.println("==============================================");

  pinMode(PIN_HV_ENABLE, OUTPUT);
  set_hv(false); // Start in standby to allow safe connection without current spike
  dacWrite(PIN_DAC, 0);

  connect_wifi();

  udp.begin(UDP_PORT);
  Serial.printf("[UDP] Listening on port %d\n", UDP_PORT);
  
  g_last_packet_ms = millis();
  Serial.println("[SYSTEM] Setup complete. Entering main loop. Waiting for host...");
}

void loop() {
  poll_udp();
  check_standby();

  if (g_hv_active) {
    drive_dac_signal();
  } else {
    // Print standby status every 5 seconds to avoid flooding
    if (millis() - g_last_status_ms > 5000) {
      Serial.printf("[STATUS] Standby active. Waiting for UDP on port %d...\n", UDP_PORT);
      g_last_status_ms = millis();
    }
    delay(10); // Sleep briefly when in standby to save CPU
  }
}
