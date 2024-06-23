# HaMQTT Protocol
_(Discussion Document)_

**An MQTT Interface to an Amateur Radio Station**

The proposed (now functional) device is apiece of ESP8266-based hardware, which sits between a Ham radio and an MQTT broker. The Ham radio receives Dual Tone Multimple Frequency (DTMF) sequences and relays them back to and MQTT broker. The hardware, then transmits any subscribed MQTT messages.
The length of teh incoming DTMF sequence is 8 characters or less. This is a nominal value seelcted due totwo assumptions:
- There is no enter key so input must be by timeout, which must be short or it becomes a pain in the ass.
- If the user is entering the charaters by hand, more than 8 charaters is a pain in the ass to type.

The protocol allows Commands and Queries. Essentially, we send contents to (Command) or request contents from (Query) virtual memory locations. Certain sequences are reserved.
Commands Start with *
Queries Start with #

## commands / Queries for the DTMF / MQTT Dialog Engine Hardware
*7 or #7 is a command or query to the engine. 
- *710 close the engine
- *711 open the engine
- *710nnnn set the auto-close timeout (nnnn=seconds)
- *711nnnn set the input timeout (nnnn=seconds)

## Long Commands / Queries
- *8 long command
- *9 data for a long command
- #8 long query
- #9 RESERVED