# iotconfig

A simple to use Arduino library for ESP32 [1] for
wireless client configuration (via Captive Portal)
supporting Arduino-OTA [2] and EEPROM/RTC variable assignment.


This following code enables IoT wireless configuration and
Arduino-OTA - there must be ensured, that the loop()
function keeps running without being blocked:

```c
#include "iotconfig.hpp"
iotConfig ic;

void setup()
{
   ic.begin("devicename", "adminpassword", 0, 0, 0);
}

void loop()
{
   ic.handle();
}
```


The WiFI SSID / PSK, friendly name and OTA password is
stored into EEPROM. The ESP32 won't need to be connected
to serial line anymore for uploading new (OTA-enabled)
sketches.

When unconfigured (or for a given time when configured),
the ESP32 turns into AP mode, so the WiFi settings can be
(re-)configured when connecting to it. The OTA-password
can only be set on delivery state (or after factory reset).

[1]; https://github.com/espressif/arduino-esp32

[2]: https://github.com/espressif/arduino-esp32/tree/master/libraries/ArduinoOTA

