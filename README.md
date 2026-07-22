# Pylontech Battery Monitoring via WiFi

This project allows you to control and monitor Pylontech US2000B, US2000C, US3000C and US5000 batteries via console port over WiFi.
It it's a great starting point to integrate battery with your home automation.

## PlatformIO setup

1. Copy `platformio.local.example.ini` to `platformio.local.ini`.
2. Set WiFi and hostname values in `platformio.local.ini`.
3. Build with `pio run` (or `py -m platformio run`).
4. Flash the first installation with `pio run --target upload`.

`platformio.local.ini` is ignored by Git. PlatformIO injects its values as build definitions. The default target is a Wemos D1 mini; change `board` in `platformio.ini` when using another ESP8266 board. The resulting OTA image is `.pio/build/esp8266/firmware.bin`.

Run `./build.ps1` to build and copy the OTA image to `firmware.bin` in the project root.

The web UI at `/` includes a drag-and-drop firmware updater. Drop the generated `firmware.bin` onto it; the ESP validates and writes the image, then reboots after success. ArduinoOTA remains available using `OTA_HOSTNAME`.

Prometheus can scrape `/metrics`. It exposes the same `power`, `battery`, and `battery_stat` metric families, names, labels, state codes, and unit conversions as Pylontech-Battery-Exporter. Each scrape queries `pwr` and `bat N`; `stat N` values are cached for one hour. The namespace defaults to `devicemon` and is changed at `/settings`.

MQTT, Loki, the Prometheus namespace, optional ESP free-heap metric, and the NTP server are configured at `/settings`. Values are stored in LittleFS and survive reboot and OTA firmware updates.

Loki support is disabled by default. Set its full `http://.../loki/api/v1/push` URI, data type, and service name, then enable it. The logger reads new battery `log` entries every five minutes and uses each entry's `ModID` as the Loki `device` label. `/settings` shows the last attempt, connection result, event counts, network route, next run, and a manual run button. The same details are available as JSON at `/logger-status`.

The dashboard displays ESP and battery time. ESP time is shown in Europe/Berlin with automatic MEZ/MESZ conversion. The sync button sets the ESP and battery clocks from the browser's local PC time.

The MQTT refresh interval is configured there and defaults to 10 seconds. `/metrics` collects a complete fresh dataset on each request and returns it as one response.

`MQTT_VALUE_PREFIX` in `platformio.local.ini` controls the legacy MQTT value prefix. With topic root `Pylontech` and prefix `US2000CBattery`, SOC is published as `Pylontech/US2000CBatterysoc`.

## Source layout

- `src/main.cpp` owns shared state and composes setup/loop.
- `src/communication.cpp` handles the Pylontech serial protocol, time sync, parsing, and battery models.
- `src/settings.cpp` handles LittleFS persistence and settings migration.
- `src/web.cpp` contains HTTP handlers, JSON output, and Prometheus exposition.
- `src/ota.cpp` handles browser firmware uploads.
- `src/mqtt.cpp` handles MQTT publishing and reconnects.
- `src/logger.cpp` polls battery events and pushes them to Loki.

The focused source files are composed through `main.cpp` as one translation unit. `build_src_filter` prevents PlatformIO from compiling them a second time. This preserves the firmware's existing shared state and memory footprint without exposing mutable globals through a broad internal API.

**I ACCEPT NO RESPONSIBILTY FOR ANY DAMAGE CAUSED, PROCEED AT YOUR OWN RISK**

# Features:
  * Low cost (around 20$ in total).
  * Adds WiFi capability to your Pylontech US2000B/C, US3000C or US5000 battery.
  * Device exposes web interface that allows to:
    * send console commands and read response over WiFi (no PC needed)
    * battery information can be retrevied also in JSON format for easy parsing
  * MQTT support:
    * device pushes basic battery data like SOC, temperature, state, etc to selected MQTT server
  * Easy to modify code using Arduino IDE and flash new firmware over WiFi (no need to disconnect from the battery).

See the project in action on [Youtube](https://youtu.be/7VyQjKU3MsU):</br>
<a href="http://www.youtube.com/watch?feature=player_embedded&v=7VyQjKU3MsU" target="_blank"><img src="http://img.youtube.com/vi/7VyQjKU3MsU/0.jpg" alt="See the project in action on YouTube" width="240" height="180" border="10" /></a>


# Parts needed and schematics:
  * [Wemos D1 mini microcontroller](https://www.amazon.co.uk/Makerfire-NodeMcu-Development-ESP8266-Compatible/dp/B071S8MWTY/).
  * [SparkFun MAX3232 Transceiver](https://www.sparkfun.com/products/11189).
  * US2000B: Cable with RJ10 connector (some RJ10 cables have only two wires, make sure to buy one that has all four wires present).
  * US2000C and US5000: Cable with RJ45 connector (see below for more details).
  * Capacitors C1: 10uF, C2: 0.1uF (this is not strictly required, but recommended as Wemos D1 can have large current spikes).

![Schematics](Schemetics.png)

# US2000C/US3000C notes:
This battery uses RJ45 cable instead of RJ10. Schematics is the same only plug differs:
  * RJ45 Pin 3 (white-green) = R1IN
  * RJ45 Pin 6 (green)       = T1OUT
  * RJ45 Pin 8 (brown)       = GND
![image](https://user-images.githubusercontent.com/19826327/146428324-29e3f9bf-6cc3-415c-9d60-fa5ee3d65613.png)


# How to get going:
  * Get Wemos D1 mini
  * Install arduino IDE and ESP8266 libraries as [described here](https://averagemaker.com/2018/03/wemos-d1-mini-setup.html)
  * Open [PylontechMonitoring.ino](PylontechMonitoring.ino) in arduino IDE
  * Make sure to copy content of [libraries subdirectory](libraries) to [libraries of your Arduino IDE](https://forum.arduino.cc/index.php?topic=88380.0).
  * Specify your WiFi login and password at the top of the file (line 13-14)
  * If you want MQTT support, uncomment line 17 and fill details in lines 21-24
  * Upload project to your device
  * Connect Wemos D1 mini to the MAX3232 transreceiver
  * Connect transreceiver to RJ10/RJ45 as descibed in the schematics (all three lines need to be connected)
  * Connect RJ10/RJ45 to the serial port of the Pylontech US2000 battery. If you have multiple batteries - connect to the master one.
  * Connect Wemos D1 to the power via USB
  * Find what IP address was assigned to your Wemos by your router and open it in the web-browser
  * You should be able now to connunicate with the battery via WiFi
