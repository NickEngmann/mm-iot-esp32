# MM-IoT-ESP32 Examples

This directory contains example applications demonstrating the Morse Micro 802.11ah (WiFi HaLow) SDK on ESP32 platforms (ESP32-C3, ESP32-C6, ESP32-S3).

## Quick Start

All examples require:
- **MMIOT_ROOT** environment variable pointing to the framework directory
- **COUNTRY_CODE** defined (default: "US" for most examples)
- Morse Micro MM6108 WiFi HaLow transceiver connected via SDIO/SPI

## Examples Overview

### 1. `porting_assistant` - Hardware Validation Tool ⭐ **START HERE**

**Purpose**: Self-test tool to validate hardware, HAL implementations, and platform setup.

**What it does**:
- Tests OS functions (malloc, time, task creation)
- Validates WLAN hardware initialization
- Tests SDIO/SPI communication
- Reads chip ID from MM6108
- Validates firmware and BCF (Board Configuration File)
- Tests data throughput on hardware interface
- Verifies busy pin functionality

**When to use**:
- **First step** when bringing up new hardware
- Diagnosing communication issues with MM6108 chip
- Validating your ESP32 ↔ MM6108 connection
- After hardware modifications

**Expected output**: Series of PASS/FAIL test results with diagnostic information

**Key files**: `main/src/porting_assistant.c`

---

### 2. `scan` - WiFi HaLow Network Scanner

**Purpose**: Scan for 802.11ah (WiFi HaLow) access points in range.

**What it does**:
- Scans all legal S1G channels for your country
- Displays discovered access points with:
  - SSID (network name)
  - BSSID (MAC address)
  - Operating bandwidth (1/2/4/8 MHz)
  - RSSI (signal strength)
  - Security type (None/PSK/SAE/OWE)
  - Beacon interval

**When to use**:
- Site surveys for 802.11ah coverage
- Verifying your HaLow AP is broadcasting
- Testing radio functionality
- Channel planning

**Configuration**:
```c
#define COUNTRY_CODE "US"  // Change to your regulatory domain
```

**Expected output**: List of discovered 802.11ah networks (if any in range)

**Key files**: `main/src/scan.c`

---

### 3. `sta_connect` - Station Connection Example

**Purpose**: Connect to a WiFi HaLow access point and receive packets.

**What it does**:
- Connects to specified SSID with passphrase
- Registers callbacks for link state changes
- Receives and logs Ethernet frames
- Displays connection status

**When to use**:
- Testing basic connectivity to HaLow AP
- Learning the MMWLAN API for connections
- Debugging association issues
- Validating WPA2-PSK/SAE/OWE security

**Configuration**:
```c
#define SSID "MorseMicro"          // Your HaLow AP SSID
#define PASSPHRASE "12345678"      // AP passphrase (comment out for OWE)
#define COUNTRY_CODE "US"
```

**Expected output**: Connection status updates and received packet information

**Key files**: `main/src/sta_connect.c`

---

### 4. `iperf` - Network Throughput Testing

**Purpose**: Measure TCP/UDP throughput using the iperf protocol.

**What it does**:
- Connects to specified HaLow AP
- Starts full network stack (DHCP, TCP/IP)
- Runs iperf in client or server mode
- Reports throughput statistics

**Modes available**:
- TCP Server (RX)
- UDP Server (RX)
- TCP Client (TX)
- UDP Client (TX)

**When to use**:
- Performance testing of HaLow link
- Range testing (measure throughput at distance)
- Network stack validation
- Comparing link configurations

**Configuration**:
```c
#define IPERF_TYPE IPERF_UDP_SERVER    // Server/client, TCP/UDP
#define IPERF_SERVER_IP "192.168.1.1"  // For client mode
#define IPERF_TIME_AMOUNT -10           // Negative = seconds, positive = bytes
```

**Additional config in**:
- `mm_app_common.c` - SSID, passphrase, network settings
- `mm_app_loadconfig.c` - Advanced network stack options

**Usage example**:
```bash
# On ESP32 (UDP server)
flash iperf example

# On AP/computer (UDP client)
iperf -c 192.168.1.2 -u -b 10M -t 30
```

**Key files**: `main/src/iperf.c`, `main/src/mm_app_common.c`

---

### 5. `sta_reboot` - Reboot Stress Test

**Purpose**: Repeatedly connect, transmit, and reboot to test stability.

**What it does**:
- Connects to HaLow AP
- Immediately sends traffic on link up
- Waits brief delay
- Reboots WLAN interface
- Repeats infinitely

**When to use**:
- Stress testing initialization/shutdown code
- Testing worst-case TX timing scenarios
- Validating memory leak fixes
- Long-term stability testing

**Configuration**:
```c
#define REBOOT_DELAY_MS 50  // Delay between reboots
```

**Expected output**: Continuous cycle of connect → transmit → reboot with statistics

**Key files**: `main/src/sta_reboot.c`

---

### 6. `transfer_reset` - Data Transfer + Reset Test

**Purpose**: Transfer a specific amount of data then reset device.

**What it does**:
- Connects to HaLow AP with full network stack
- Runs iperf to transfer specified data amount
- Prints transfer report
- Resets entire device (not just WLAN interface)
- Repeats on boot

**When to use**:
- Testing specific data transfer amounts before reset
- Firmware stability testing
- Automated cycling tests
- Debugging transfer-related issues

**Configuration**: Same as `iperf` example plus automatic reset on completion

**Example usage**:
```bash
# On AP/computer, run this loop to continuously test
attempt=0
while :; do
  printf "\n#### Attempt $attempt ####\n"
  iperf -c 192.168.1.2 -n 2M  # 2MB transfer
  let "attempt++"
  sleep 15
done
```

**Key files**: `main/src/transfer_reset.c`

---

### 7. `rf-test` - RF Testing Tool (Advanced)

**Purpose**: Low-level RF testing via UART interface.

**What it does**:
- Listens on dedicated UART port
- Executes ATE (Automated Test Equipment) commands
- Enables TX/RX testing, calibration, certification
- Uses SLIP protocol for command framing

**When to use**:
- RF characterization and certification
- Manufacturing test procedures
- Regulatory testing (FCC, CE, etc.)
- Advanced debugging with Morse support

**Requirements**:
- External `morsectrl` tool
- Dedicated UART connection
- Coordination with Morse Micro support

**Configuration**: Define `CONFIG_RF_TEST_UART_PORT_NUM` in sdkconfig

**Note**: Not intended for general application development. Contact Morse Micro support for documentation.

**Key files**: `main/src/rf-test.c`

---

## Getting Started Guide

### Step 1: Hardware Validation
```bash
cd porting_assistant
# Configure for your target (e.g., esp32c3)
idf.py set-target esp32c3
idf.py build flash monitor
```
Verify all tests PASS before proceeding.

### Step 2: Scan for Networks
```bash
cd ../scan
idf.py build flash monitor
```
Confirm you can see your HaLow AP (or verify radio is working).

### Step 3: Connect to AP
```bash
cd ../sta_connect
# Edit main/src/sta_connect.c to set your SSID/passphrase
idf.py build flash monitor
```
Verify connection succeeds.

### Step 4: Test Throughput
```bash
cd ../iperf
# Edit main/src/mm_app_common.c for SSID/passphrase
# Configure iperf mode in main/src/iperf.c
idf.py build flash monitor
```

## Common Configuration Files

### `mm_app_regdb.c`
- Regulatory database with channel lists per country
- Defines legal frequencies and power limits
- Modify to add custom regulatory domains

### `mm_app_common.c` (iperf/transfer_reset)
- Network configuration (SSID, passphrase, security)
- DHCP settings
- IP stack initialization

### `mm_app_loadconfig.c` (iperf/transfer_reset)
- Advanced network stack tuning
- Buffer sizes, TCP parameters
- Power save settings

### `sdkconfig.defaults.*`
- ESP-IDF configuration per target chip
- Partition tables, flash size
- FreeRTOS settings

## Building for Different Targets

```bash
# ESP32-C3 (RISC-V, single core)
idf.py set-target esp32c3

# ESP32-C6 (RISC-V, WiFi 6)
idf.py set-target esp32c6

# ESP32-S3 (Xtensa, dual core)
idf.py set-target esp32s3
```

Each target uses corresponding `sdkconfig.defaults.esp32XX` file.

## Troubleshooting

### "Could not find specified regulatory domain"
- Check `COUNTRY_CODE` is defined in source
- Verify country code exists in `mm_app_regdb.c`

### "Chip ID read failed"
- Check SDIO/SPI connections between ESP32 and MM6108
- Run `porting_assistant` to diagnose hardware issues
- Verify voltage levels and power supply

### "Scan completed" but no results
- No 802.11ah APs in range (this is NOT regular WiFi!)
- Verify your AP is broadcasting on HaLow frequencies
- Check antenna connection
- Verify country code matches AP configuration

### Build errors with component dependencies
- Ensure `MMIOT_ROOT` environment variable is set
- Check `.vscode/settings.json` has correct paths
- All `idf_component.yml` files should use `override_path`

## Additional Resources

- Morse Micro documentation in `../../framework/`
- ESP-IDF documentation: https://docs.espressif.com/projects/esp-idf/
- 802.11ah (WiFi HaLow) standard overview

## License

All examples are Copyright 2022-2025 Morse Micro, licensed under Apache-2.0.
