# HaMQTT
_Pronouced Ham Cue Tee Tee_

**An MQTT Interface to an Amateur Radio Station**

Triggered by [WO4ROB's DTMF relay controller](http://51454.nodes.allstarlink.org/DTMF-Remote-Control.html)

I want to use a handheld radio transceiver to publish MQTT message to broker. Any device on my home IoT setup, then, can subscribe to and act on these messages. The devices may publish MQTT messages to the broker, and the handheld will subscribe to those messages and broadcast them as voice messages.

The IoT portion of the circuit will run [TASMOTA](https://tasmota.github.io/), an ESP8266-based firmware which can act as a WiFi access point for an [MQTT broker](https://en.wikipedia.org/wiki/MQTT) installed elsewhere.

## To do:
- Reduce the size of the hardware. (Prototype circuit has a lot of wire links, etc.)
- Can the Arduino perform teh DTMF decoding? (The would allow the removal of teh DTMF hardware.)
- Can TASMOTA be modified to directly support the device? (This would mean only one MCU required.)

## History
### v0.90 20240223

The MCU, DTMF decoder and handheld interface hardware are complete. This version of the firmware receives a DTMF sequence and retransmits it as a verbal copy.
