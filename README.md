# ESP32 Magic Eye - WiFi CPU Monitor

A hardware project that bridges vintage 1950s technology with modern PC hardware. This repository contains the ESP32 firmware and Python host script to repurpose a vacuum-tube **Magic Eye** (tuning indicator, typically EM34, 6E5, EM84, or similar) as a real-time WiFi-connected CPU load monitor.

As your computer's CPU load increases, the green glowing eye of the tube physically closes or opens wider to represent the system load.

---

## How It Works

Magic Eye tubes require a variable negative DC voltage (usually 0V to -10V or more) on their control grid to adjust the width of the shadow. Modern hobbyist Magic Eye driver boards (commonly found on AliExpress or eBay) include a built-in AC-coupling capacitor and a peak detector circuit so they can be driven by a standard line-level audio signal (AC).

Because the board expects an AC audio signal, **sending a static DC voltage from the ESP32's DAC will not work** (it gets blocked by the input capacitor).

This project solves that by generating a **low-frequency square wave (~1.5 Hz)** from the ESP32's DAC. The amplitude of this square wave is proportional to the CPU load. The peak detector on the tube's driver board charges up during the high phase and discharges during the low phase, causing the green eye to open and close dynamically. This recreates the iconic "bouncing needle" movement of vintage analog equipment.

```
+------------------+                   +------------------+
|   Host PC        |                   |   ESP32          |
| (Python + psutil)|                   | (WiFi Receiver)  |
|                  |   WiFi / UDP      |                  |
| Samples CPU %    | ----------------> | Generates        |
| Maps to 0-255    |   (Single Byte)   | 1.5 Hz wave      |
+------------------+                   +--------+---------+
                                                |
                                                | DAC Output (GPIO 25)
                                                v
+------------------+                   +------------------+
| Magic Eye Tube   |                   | VU Driver Board  |
|                  | <---------------- |                  |
| Shadow changes   |    Grid Voltage   | AC Coupling +    |
| visual width     |                   | Peak Detector    |
+------------------+                   +------------------+
```

---

## Features

- **Real-Time Visualization**: Smooth tracking of system load (CPU usage).
- **WiFi-Connected**: No USB tether required once flashed. The tube can be placed anywhere in the room.
- **Auto-Standby Watchdog**: If the Python script stops or the PC shuts down, the ESP32 automatically detects the lack of UDP packets after 5 seconds and powers down the high-voltage module to preserve the tube's phosphors. It wakes up instantly when packets resume.
- **Security-First Configuration**: WiFi credentials are kept in a separate local header file (`secrets.h`) which is ignored by Git, ensuring you don't accidentally leak your credentials.

---

## Hardware Components

| Component | Description | Qty | Notes |
|---|---|---|---|
| **ESP32 Dev Board** | Dual-core MCU with built-in WiFi and 8-bit DACs. | 1 | Standard 30-pin or 38-pin module. |
| **Magic Eye VU Module** | Driver board with tube socket (often sold with an EM34, 6E5, or EM84). | 1 | Look for "Magic Eye VU level meter" on AliExpress. |
| **MT3608 DC-DC Booster** | Steps up 5V USB to 6.3V for the tube's heater/filament. | 1 | Crucial for tube longevity. |
| **HV DC-DC Boost Module** | High-voltage booster (5V input, ~250V DC output). | 1 | Drives the tube's target/anode plate. |
| **Cables & Hookup Wires** | Jumper wires and hookup wires. | — | Thick insulation recommended for HV lines. |

---

## Power and Wiring

### Block Diagram

![System Block Diagram](blockdiagram.png)

### Detailed Pin Connections

#### 1. Filament Power (MT3608 Boost Module)
*Before connecting to the tube module, power the MT3608 and adjust the small brass screw on its potentiometer until the output voltage reads **exactly 6.3 V**. Overvoltage will burn the tube's filament!*

| MT3608 Pin | Connected to |
|---|---|
| **IN+** | +5V (USB / ESP32 VIN) |
| **IN-** | GND |
| **OUT+** | Tube filament input `Vf+` |
| **OUT-** | Common GND / Tube `Vf-` |

#### 2. High Voltage Power (HV Boost Module)
*The module should only turn on when the ESP32 instructs it. Connect the Enable pin of the HV module to the ESP32 so the software watchdog can turn off the 250V rail during standby.*

| HV Boost Pin | Connected to |
|---|---|
| **VIN / IN+** | +5V (USB) |
| **GND / IN-** | Common GND |
| **ENABLE** | ESP32 GPIO 4 (High = Active) |
| **VOUT / HV+** | Tube target/anode input `Va` |
| **GND OUT** | Common GND |

#### 3. Signal Interface (Magic Eye VU Board)

| VU Board Pin | Connected to | Description |
|---|---|---|
| **Vf+** | MT3608 OUT+ | Heater filament positive (6.3V) |
| **Vf-** | Common GND | Heater filament return |
| **Va** | HV Boost VOUT | Anode/Target supply (~250V) |
| **IN_L** | ESP32 GPIO 25 | Left Channel input (drives the tube) |
| **IN_R** | Unconnected | Right Channel input (leave open) |
| **GND (Audio)**| Common GND | Signal reference ground |

---

## Software Installation & Setup

### 1. ESP32 Firmware Compilation

This project uses **PlatformIO** for dependency and build management.

1. Navigate to the firmware directory:
   ```bash
   cd firmware/cpu_monitor/
   ```
2. Create your local credentials file by copying the template, or just edit/create `src/secrets.h`:
   ```cpp
   #ifndef SECRETS_H
   #define SECRETS_H

   #define WIFI_SSID     "YOUR_WIFI_NAME"
   #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

   #endif
   ```
3. Connect your ESP32 to the computer via USB and upload the firmware:
   ```bash
   pio run --target upload
   ```
4. Open the serial monitor to read the IP address assigned to the ESP32:
   ```bash
   pio device monitor
   ```
   *Note down the IP address printed on connection, e.g. `Connected - IP: 192.168.1.42`.*

### 2. Python Host Client Setup (Linux / Fedora)

The host script queries local CPU metrics using the `psutil` library and streams them to the ESP32.

1. Navigate to the host directory:
   ```bash
   cd host/cpu_monitor/
   ```
2. Create and activate a Python virtual environment to keep your system packages clean:
   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   ```
3. Install the dependencies:
   ```bash
   pip install -r requirements.txt
   ```
4. Start streaming CPU statistics to your Magic Eye (replace the IP with your ESP32's IP):
   ```bash
   python monitor.py --host 192.168.1.42 --verbose
   ```
   The `--verbose` flag displays a real-time ASCII bar showing current load in your terminal:
   ```
   CPU  14.2%  [###-----------------]  -> byte  36
   ```

---

## Calibration and Tuning

The Magic Eye VU board has two onboard potentiometers (trimmers):
1. **Sensitivity (Gain)**: Adjusts how much the eye closes in response to the ESP32's signal. Run the PC at 100% CPU (e.g. compile something or run a benchmark) and turn the sensitivity trimmer until the glowing segments just barely meet.
2. **Shape (Symmetry/Bias)**: Adjusts the default angle/aperture of the shadow when no signal is present. Stop the Python script (0% CPU) and adjust this trimmer until the eye is wide open without overlapping.

---

## Safety Guidelines

> ⚠️ **HIGH VOLTAGE WARNING**: The DC-DC boost module generates **250V DC**. High voltage can cause severe shock or death.
> - Never adjust wiring, solder, or touch the circuit boards while the device is powered.
> - Always house the high-voltage module and tube socket inside an insulated enclosure.
> - Wait at least 30 seconds after powering down before touching the components, as capacitors can store dangerous charges.

---

## License & Disclaimer

This is a personal hobbyist project. Build and operate at your own risk. The author is not responsible for any damage to your hardware, computer, or self.
