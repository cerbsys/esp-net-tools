# Cerbsys NetTools

**A portable network diagnostics toolkit built for the ESP32-2432S028 Cheap Yellow Display (CYD).**

Cerbsys NetTools is a touchscreen-operated network diagnostics toolkit designed to bring essential networking utilities to a compact ESP32 device.

Built specifically for the ESP32-2432S028 CYD, it provides quick access to network troubleshooting tools through an integrated Wi-Fi connection and touchscreen interface.

## Hardware

Designed for the **ESP32-2432S028 Cheap Yellow Display (CYD)**.

| Component | Specification |
|---|---|
| Development Board | ESP32-2432S028 CYD |
| Microcontroller | ESP32 |
| Display | 2.8-inch TFT |
| Resolution | 240 × 320 |
| Display Driver | ILI9341 |
| Touch Controller | XPT2046 |
| Wireless Connectivity | 2.4 GHz Wi-Fi |

## Features

### Wi-Fi Connectivity
Connect the device to wireless networks to access its network diagnostic utilities / Wireless connectivity not required.

### Wi-Fi Scanner
Scan nearby wireless networks and view available network information.

### DNS Lookup
Perform DNS queries to resolve hostnames and retrieve IP addresses.

### Ping
Test connectivity to network hosts and measure response times.

### HTTP Check
Test HTTP connectivity to websites and network services.

### Port Scanner
Scan target hosts for accessible network ports.

### Traceroute
Trace the network path between the device and a destination host.

### Requirements

- Visual Studio Code
- PlatformIO IDE extension
- ESP32-2432S028 CYD
- USB data cable

### Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/cerbsys/esp-net-tools.git
   ```

2. Open the project folder in Visual Studio Code.
3. Install the PlatformIO IDE extension.
4. Allow PlatformIO to install the required dependencies.
5. Connect your ESP32-2432S028 CYD.
6. Build and upload the firmware using PlatformIO.

## Project Structure

```text
esp-net-tools/
├── include/       # Header files
├── lib/           # Project libraries
├── src/           # Application source code
├── test/          # Unit tests
├── platformio.ini # PlatformIO configuration
└── README.md
```

## Compatibility

Currently designed specifically for the:

**ESP32-2432S028 Cheap Yellow Display (CYD)**

Other ESP32 development boards are not officially supported.

---

*Cerbsys NetTools — Network diagnostics, wherever you go.*
