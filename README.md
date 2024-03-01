# HaMQTT
_Pronouced Ham Cue Tee Tee_

**An MQTT Interface to an Amateur Radio Station**

Triggered by [WO4ROB's DTMF relay controller](http://51454.nodes.allstarlink.org/DTMF-Remote-Control.html)

I want to use a handheld radio transceiver to publish MQTT message to broker. Any device on my home IoT setup, then, can subscribe to and act on these messages. The devices may publish MQTT messages to the broker, and the handheld will subscribe to those messages and broadcast them as voice messages.

The IoT portion of the circuit will run [TASMOTA](https://tasmota.github.io/), an ESP8266-based firmware which can act as a WiFi access point for an [MQTT broker](https://en.wikipedia.org/wiki/MQTT) installed elsewhere.

## To do:
- Add the voice response.
- Add the ESP32 board, flashed with TASMOTA, hooked to teh serial port.
-Add teh MQTT publish / subscribe.
- Reduce the size of the hardware. (Prototype circuit has a lot of wire links, etc.)
- Can the Arduino perform the DTMF decoding? (The would allow the removal of the DTMF hardware.)
- Can TASMOTA be modified to directly support the device? (This would mean only one MCU required.)

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
