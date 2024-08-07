# HaMQTT
_Pronouced Ham Cue Tee Tee_

**An MQTT Interface to an Amateur Radio Station**

Triggered by [WO4ROB's DTMF relay controller](http://51454.nodes.allstarlink.org/DTMF-Remote-Control.html)

This project is a suite of hardware / firmware / software to allow the use of a handheld (or other) radio transceiver to publish MQTT messages to a broker. Any device on my home IoT setup, then, can subscribe to and act on the published messages. The devices may publish MQTT messages to the broker, and the handheld will subscribe to those messages and broadcast them as voice messages, or otherwise act accordingly.

Initially, I had used [TASMOTA](https://tasmota.github.io/) to act as the WiFi access point for the [MQTT broker](https://en.wikipedia.org/wiki/MQTT) traffic. Now, we have a piece of custom WiFi / MQTT / Ham radio hadrware / firmware instead.

## To do:
- Remove the MY8870 DTMF decoder module, by incorporating its functionality into the firmware.
  - Can the Arduino perform the DTMF decoding? (The would allow the removal of the DTMF hardware.)
- Consider adding audio streaming, allowing actual voice samples rather than electronic voice.

## History
### v0.90.01 20240223
- Test program using WO4ROB's schematic and software, but without the relay outputs. It proves the principle.

### v0.90.02 20240224
- My hardware still doesn't have the ESP8266 / TASMOTA function yet, but I've coded a robust way to receive a DTMF string rather than single digits. 

### v0.90.03 20240225
- Added the LED functionality. Got it to rebroadcast the sequence in voice.

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

### v0.93.10 20240403
- Trying to patch this back together to work on an ESP8266 without the Arduino, TASMOTA, or MT8770.

### v0.94.11 20240509
- Ported everything over to a single Arduino: MQTT functioning on ESP8288 (D1 Mini), using MT8870.

### v0.94.12 20240610
- This has some debug code to generate beeps. Next version will remove that.

### v0.94.13 20240616
- Working implementation of https://github.com/earlephilhower/ESP8266Audio.

### v0.94.14 20240624
- Better implementation of ESP8266Audio.
- Demonstrates ability to output a received MQTT string. (A string message published to topic mqtt/tx will be transmitted by the radio.)
- Groundwork started on publishing MQTT as a python-style dictionary.
- Breaking change: Start of implementation of input (command / query) protocol. (*701 and *700 used to open and close communications.)

### v0.95.00 20240626
- Implemented Auto-close.
- Now publishes an MQTT message on receipt of a DTMF sequence, if the relay is open.
- Periodically publishes an MQTT status message, and network information, even if relay is closed.
- Added in some QSO / no QSO counters just for sport.

### v0.95.01 20240708
- Added a Timer class, since the timer code is a bit repetative; makes it easier to manage.
- Added automatic callsign broadcast on an infrequent basis, based on RSGB recommendation.

### v0.95.02 20240708
- Shortened some key values in MQTT dictionaries.
- Moved the time client updater into the slowest loop.
- Added certain user modifiable parameters to EEPROM.

### v0.95.03 20240719
- The string components of "EEPROM" have been commented out as they don't work.
