# Station HTTP Server Example

## Overview

This example demonstrates a complete Wi-Fi HaLow (802.11ah) station implementation with an embedded HTTP web server running on an ESP32-S3 with the Morse Micro MM6108A1 chipset. The application showcases the full capabilities of the MM-IoT-SDK for building connected IoT devices that leverage Sub-GHz Wi-Fi technology.

## Features

### Wi-Fi HaLow Connectivity
- **AP Scanning**: Scans for available Wi-Fi HaLow (802.11ah) access points
- **Station Mode**: Connects to a configured AP with security support (Open, PSK, SAE, OWE)
- **DHCP Client**: Automatically obtains IP address configuration
- **Power Management**: Disabled power save mode for boards without BUSY pin wiring (e.g., Seeed Xiao HaLow)
- **Link Monitoring**: Reliable connection establishment with timeout handling

### HTTP Web Server
- **Responsive Web Interface**: Clean, mobile-friendly HTML interface
- **Real-time Status**: Displays network information (IP, netmask, gateway)
- **Device Information**: Shows Morse Micro chipset details
- **RESTful API**: JSON endpoints for programmatic control

### GPIO Control
- **LED Management**: Control onboard LED (GPIO 21) via web interface
- **Toggle Control**: Turn LED on/off with visual feedback
- **Blink Demo**: Animated LED sequence demonstration
- **Hardware Abstraction**: Handles inverted GPIO logic (LOW=ON, HIGH=OFF)

## Hardware Requirements

- **ESP32-S3 Development Board**: Any ESP-IDF compatible ESP32-S3 board
- **Morse Micro MM6108A1**: Wi-Fi HaLow transceiver module
- **Onboard LED**: Connected to GPIO 21 (configurable)
- **Wi-Fi HaLow Access Point**: 802.11ah AP with DHCP server

### Tested Hardware
- Seeed Studio XIAO ESP32-S3 with Morse Micro MM6108A1 module
- Other ESP32-S3 boards with compatible Morse Micro modules

## Software Architecture

```
┌─────────────────────────────────────────┐
│           Application Layer             │
│  ┌─────────────┐     ┌───────────────┐ │
│  │ HTTP Server │     │ LED Control   │ │
│  │ (esp_http)  │     │ (GPIO Driver) │ │
│  └──────┬──────┘     └───────┬───────┘ │
│         │                    │         │
├─────────┴────────────────────┴─────────┤
│       Network Stack (MMIPAL/LwIP)      │
├────────────────────────────────────────┤
│     Wi-Fi HaLow (mmwlan interface)     │
├────────────────────────────────────────┤
│   Morse Micro MM6108A1 Hardware Driver │
│        (SDIO/SPI Communication)        │
└────────────────────────────────────────┘
```

## Building and Flashing

### Prerequisites

1. **Set up ESP-IDF environment**:
   ```bash
   . $IDF_PATH/export.sh
   ```

2. **Set MMIOT_ROOT environment variable**:
   ```bash
   export MMIOT_ROOT=/path/to/mm-iot-esp32/framework
   ```

3. **Configure Wi-Fi credentials**:
   Edit `main/src/mm_app_common.h` or use menuconfig:
   ```bash
   idf.py menuconfig
   ```
   Navigate to: `Component config → Morse Micro → Wi-Fi Configuration`

### Build Process

```bash
# Navigate to project directory
cd examples/station_http_server

# Set target (ESP32-S3)
idf.py set-target esp32s3

# Build the project
idf.py build

# Flash to device
idf.py -p /dev/ttyACM0 flash

# Monitor output
idf.py -p /dev/ttyACM0 monitor
```

### Build Output

After a successful build, you'll find:
- **Binary**: `build/station_http_server.bin`
- **Partition Table**: `build/partition_table/partition-table.bin`
- **Bootloader**: `build/bootloader/bootloader.bin`

## Configuration

### Wi-Fi Settings

The application uses configuration defined in `mm_app_common.h`:

```c
#define DEFAULT_SSID        "YourHaLowAP"
#define DEFAULT_PASSPHRASE  "YourPassword"
#define DEFAULT_SECURITY    MMWLAN_SAE  // or MMWLAN_PSK, MMWLAN_OPEN
```

### HTTP Server Settings

Configure in `station_http_server.c`:

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.server_port = 80;           // Default HTTP port
config.stack_size = 8192;          // Stack size for handlers
config.lru_purge_enable = true;    // Enable connection cleanup
```

### LED GPIO

Change the LED pin in `station_http_server.c`:

```c
#define LED_GPIO 21  // Modify for your board
```

## Usage

### Accessing the Web Interface

1. **Flash and boot the device**
2. **Monitor serial output** to see the connection process:
   ```
   Morse HaLow Web Server Demo (Built Jan 20 2025 14:30:00)

   ===== Initializing Wi-Fi HaLow Interface =====
   Disabling power save mode (Seeed Xiao HaLow requirement)...

   ===== Connecting to Wi-Fi =====
   Connecting to SSID: YourHaLowAP
   Link status: CONNECTED
   IP Address obtained: 192.168.1.100

   ===== Successfully Connected with IP Address! =====

   ===== Starting Web Server =====
   HTTP server started successfully!
   Access the web page at: http://192.168.1.100

   *** Web server is running! ***
   *** Visit http://192.168.1.100 in your browser ***
   ```

3. **Open a web browser** and navigate to the displayed IP address
4. **Interact with the web interface**:
   - View network status and device information
   - Toggle LED on/off
   - Run LED blink demo

### HTTP API Endpoints

#### GET `/`
Returns the main HTML page with device status and controls.

**Response**: HTML page

#### GET `/led/toggle`
Toggles the LED state (ON → OFF or OFF → ON).

**Response**: `LED ON` or `LED OFF`

**Example**:
```bash
curl http://192.168.1.100/led/toggle
```

#### GET `/led/blink`
Runs a 5-cycle LED blink demonstration (200ms on/off).

**Response**: `Blink complete`

**Example**:
```bash
curl http://192.168.1.100/led/blink
```

## Troubleshooting

### Connection Issues

**Problem**: Device fails to connect to AP
- **Solution**: Verify AP SSID and password in configuration
- **Check**: Ensure AP is broadcasting on a supported HaLow channel
- **Verify**: AP is within range (HaLow has ~1km range outdoors)

**Problem**: "Failed to disable power save mode"
- **Cause**: Power save command timing issue
- **Impact**: May still work, but connection could be unreliable
- **Solution**: Ensure power save is disabled before `app_wlan_start()`

### Web Server Issues

**Problem**: "Failed to start HTTP server"
- **Solution**: Check that port 80 is not in use
- **Verify**: Sufficient memory available (monitor heap in serial output)
- **Check**: ESP event loop initialized before server start

**Problem**: Cannot access web page
- **Verify**: Device obtained IP address (check serial monitor)
- **Check**: Client device is on the same network
- **Test**: Ping the device IP address first
- **Firewall**: Ensure no firewall blocking HTTP traffic

### LED Control Issues

**Problem**: LED doesn't respond to commands
- **Check**: GPIO pin number matches your hardware
- **Verify**: LED polarity (some boards use inverted logic)
- **Test**: LED works with manual gpio_set_level() calls

## Code Structure

```
station_http_server/
├── CMakeLists.txt              # Root build configuration
├── README.md                   # This file
├── sdkconfig.defaults          # Default ESP-IDF configuration
├── partitions.csv              # Custom partition table (2MB app)
└── main/
    ├── CMakeLists.txt          # Main component build config
    ├── idf_component.yml       # Component dependencies
    └── src/
        ├── station_http_server.c    # Main application logic
        ├── mm_app_common.h          # Wi-Fi configuration
        └── mm_app_regdb.c           # Regulatory domain database
```

## Key Functions

### Wi-Fi Functions
- `app_wlan_init()`: Initialize Wi-Fi HaLow interface and IP stack
- `app_wlan_start()`: Connect to configured AP and obtain IP address
- `mmwlan_set_power_save_mode()`: Configure power management
- `scan_rx_callback()`: Handle scan result notifications
- `scan_complete_callback()`: Handle scan completion

### HTTP Server Functions
- `start_webserver()`: Initialize and start HTTP server
- `root_get_handler()`: Handle GET requests to `/`
- `led_toggle_handler()`: Handle GET requests to `/led/toggle`
- `led_blink_handler()`: Handle GET requests to `/led/blink`

### LED Control Functions
- `led_init()`: Configure GPIO for LED control
- `led_set(bool on)`: Set LED state (handles inverted logic)
- `led_toggle()`: Toggle LED state
- `led_blink_demo()`: Run 5-cycle blink sequence

## Performance Characteristics

- **Boot to Connected**: ~5-10 seconds (depending on AP response)
- **HTTP Response Time**: <100ms for simple GET requests
- **Memory Usage**: ~150KB DRAM, ~2MB flash (includes web server stack)
- **Concurrent Connections**: Up to 4 (configurable in httpd_config)
- **Power Consumption**: ~100mA active (power save disabled)

## Customization Ideas

### Extend the Web Interface
- Add sensor data display (temperature, humidity, etc.)
- Implement real-time updates with JavaScript fetch polling
- Add WebSocket support for push notifications
- Create a multi-page web application

### Add More GPIO Controls
- Control multiple LEDs or relays
- Read button/switch states
- PWM control for dimming or motor speed
- Analog input monitoring (ADC)

### Network Features
- mDNS service discovery (access via http://device-name.local)
- MQTT client for IoT platform integration
- OTA firmware updates over HTTP
- REST API for IoT device management

### Security Enhancements
- HTTPS with TLS certificates
- HTTP authentication (Basic or Digest)
- API token authentication
- Rate limiting for API endpoints

## Related Examples

- **sta_connect**: Basic station connection without HTTP server
- **iperf**: Network performance testing with throughput measurements
- **scan**: Basic Wi-Fi HaLow AP scanning example

## License

This example is licensed under the Apache 2.0 license. See the LICENSE file in the repository root for details.

## Support

For issues, questions, or contributions:
- **Morse Micro Documentation**: https://docs.morsemicro.com
- **ESP-IDF Documentation**: https://docs.espressif.com/projects/esp-idf
- **GitHub Issues**: Report bugs or request features

## Version History

- **v2.9.7** (2025-01): Initial station_http_server example with LED control
- Added power save disable for Seeed Xiao HaLow compatibility
- Enhanced web interface with responsive design
