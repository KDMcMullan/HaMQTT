# HaMQTT
_Pronouced Ham Cue Tee Tee_

**An MQTT Interface to an Amateur Radio Station**

Triggered by [WO4ROB's DTMF relay controller](http://51454.nodes.allstarlink.org/DTMF-Remote-Control.html)

This project is a suite of hardware / firmware / software to allow the use of a handheld (or other) radio transceiver to publish MQTT messages to a broker. Any device on my home IoT setup, then, can subscribe to and act on the published messages. The devices may publish MQTT messages to the broker, and the handheld will subscribe to those messages and broadcast them as voice messages, or otherwise act accordingly.

Initially, I had used [TASMOTA](https://tasmota.github.io/) to act as the WiFi access point for the [MQTT broker](https://en.wikipedia.org/wiki/MQTT) traffic. Now, we have a piece of custom WiFi / MQTT / Ham radio hadrware / firmware instead.

## To do:
- Remove the MY8870 DTMF decoder module, by incorporating its functionality into the firmware.
- Can the Arduino perform the DTMF decoding? (The would allow the removal of the DTMF hardware.)
- Consider adding audio streaming, allowing actual voice samples ratehr than electronic voice.


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

### v0.92.06 20240306
- Refactored MQTTresponse for MQTTout to distinguish it later from incoming.
- Added a software serial port for transmission of DTMF string back to TASMOTA on the ESP8266. TASMOTA automatically publishes this by MQTT.

### v0.92.08 20240310
- Modified Serial Receive so it can't get stuck in a loop.
- Refactored MQTTout for serOut, and MQTTin for serIn.
- Changed back to ABCD*# instead of ABCDEF. We'll work in decimal instead of hex.
- Formatted serial output into something resembling a JSON dictionary.

