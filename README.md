# HaMQTT
_Pronouced Ham Cue Tee Tee_

**An MQTT Interface to an Amateur Radio Station**

Triggered by [WO4ROB's DTMF relay controller](http://51454.nodes.allstarlink.org/DTMF-Remote-Control.html)

I want to use a handheld radio transceiver to publish MQTT message to broker. Any device on my home IoT setup, then, can subscribe to and act on these messages. The devices may publish MQTT messages to the broker, and the handheld will subscribe to those messages and broadcast them as voice messages.

The IoT portion of the circuit will run [TASMOTA](https://tasmota.github.io/), an ESP8266-based firmware which can act as a WiFi access point for an [MQTT broker](https://en.wikipedia.org/wiki/MQTT) installed elsewhere. The hardware for this is likely to be an ESP-01 or similar.

## To do:
- Add the ESP32 board, flashed with TASMOTA, hooked to the serial port. NB, for ESP-01, connect GPOI0 LOW, it's 3.3V and don't forget to connect CH_PD to HI. See also https://templates.blakadder.com/ESP-01S.html
- Add the MQTT publish / subscribe.
- Reduce the size of the hardware. (Prototype circuit has a lot of wire links, etc.)
- Can the Arduino perform the DTMF decoding? (The would allow the removal of the DTMF hardware.)
- Can TASMOTA be modified to directly support the device? (This would mean only one MCU required.)
- Consider shifting the project onto a Rapberry Pi with USB sound card. RPi can decode DTMF in software and can output actual voice MP3s.

## History
### v0.90.01 20240223

Test program using WO4ROB's schematic and software, but without the relay outputs. It proves the principle.

### v0.90.02 20240224
My hardware still doesn't have the ESP8266 / TASMOTA function yet, but I've coded a robust way to receive a DTMF string rather than single digits. 

### v0.90.03 20240225

Added the LED functionality. Got it to rebroadcast the sequence in voice.

### v0.90.04 20240229

- Started building the MQTT response protocol.
- Parser now detects sequences to open and close the relay.
- Timer to auto-close the relay to commands is implemented.
- Timer to ensure callsign is broadcast periodically is implemented.

### v0.91.05 20240303

- Fixed a bug with message acknowledge where zero had been written to the the 0th item of the string.
- Audio responses for open, close, acknowledge, no acknowledge, all working.
- Callsign added to outgoing Tx at the allotec delta time.
