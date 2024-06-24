/****************************************************************************/
/*
/* HaMQTT (Ham Cue Tee Tee)
/* A DTMF / MQTT / Voice Relay for an Amateur Radio Station
/*
/* Ken McMullan
/* Inspired by WO4ROB
/* v0.92.08 20240310
/*
/****************************************************************************/
/*
/* History
/*
/* v0.90.01 20240223
/* Test program using WO4ROB's schematic and software, but without the relay
/* outputs. It proves the principle.
/*
/* v0.90.02 20240224
/* My hardware still doesn't have the ESP8266 / TASMOTA function yet, but
/* I've coded a robust way to receive a DTMF string rather than single
/* digits. 
/*
/* v0.90.03 20240225
/* Added the LED functionality. Got it to rebroadcast the sequence in voice.
/*
/* v0.90.04 20240229
/* Started building the MQTT response protocol.
/* Parser now detects sequences to open and close the relay.
/* Timer to auto-close the relay to commands is implemented.
/* Timer to ensure callsign is broadcast periodically is implemented.
/*
/* v0.91.05 20240303
/* Fixed a bug with message acknowledge where zero had been written to the
/* the 0th item of the string.
/* Audio responses for open, close, acknowledge, no acknowledge, all working.
/* Callsign added to outgoing Tx at the aloted delta time.
/* Sketch uses 7270 bytes (22%) of program storage space. Maximum is 32256
/* bytes.
/* Global variables use 894 bytes (43%) of dynamic memory, leaving 1154 bytes
/* for local variables. Maximum is 2048 bytes.
/*
/* v0.92.06 20240306
/* Refactored MQTTresponse for MQTTout to distinguish it later from incoming.
/* Added a software serial port for transmission of DTMF string back to
/* TASMOTA on the ESP8266. TASMOTA automatically publishes this by MQTT.
/*
/* v0.92.07 20240308
/* Basic serial port receiver implemented.
/*
/* v0.92.08 20240310
/* Modified Serial Receive so it can't get stuck in a loop.
/* Refactored MQTTout for serOut, and MQTTin for serIn.
/* changed back to ABCD*# instead of ABCDEF. We'll work in decima instead
/* of hex.
/* Formatted serial output into something resembling a JSON dictionary.
/*
/****************************************************************************/
/*
/* To Do
/*
/* NB if the serial string is longer than serLen, the program will crash.
/* The string {"type":"STATUS","DATA":"12345678"} is already 35 characters.
/*
/* Implement better serIn pub / sub.
/* Implement address / data set / get protocol.
/* Once protocol supports local settings, store timeouts, etc in EEPROM.
/*
/****************************************************************************/
/*
/* Notes:
/*
/* Don't forget that the connected radio must have the volume up. A setting
/* between 1/4 and 3/4 is suggested.
/*
/****************************************************************************/

// Talkie library
// https://github.com/going-digital/Talkie
// Copyright 2011 Peter Knight
// This code is released under GPLv2 license.
// The output is pin 3; it can be connected via a 1uF capacitor to the mic
// input.
// Follow the instrouctions on GitHub for installation. You also need file
// talkie.h in the Arduino/libraries folder.

#include "talkie.h" // Arduino/libraries/Talkie/talkie.h

// This file contains the words or voice for talkie
// Remove the comment marks, within the file, only for the words you use, to save memory space

#include "voice.h" // Arduino/libraries/Voice/voice.h

// NB pin 3 is reserved for output as a Talkie specific PWM audio output

#include "SoftwareSerial.h"

#define Q1   8 // bit 0 input (direct from DTMF decoder)
#define Q2   7 // bit 1 input
#define Q3   6 // bit 2 input
#define Q4   5 // bit 3 input
#define STQ  4 // steering input

#define swTx 10 // software UART Tx
#define swRx 11 // software UART Rx

#define PTT  2 // PTT output (to PTT via optocoupler)

#define LED 13 // onboard LED

#define openStr  "*12A" // DTMF sequence to open the relay ex E12A
#define closeStr "####" // DTMF sequence to close the relay exFFFF

#define closeTime 300000 // milliseconds before auto-close (5min)
#define callTime  120000 // milliseconds between callsign broadcast (2min)

#define interTime  3000 // time in ms allowed between characters before timeout
#define flashTime   250 // half rate in ms of flashing LED

#define DTMFlen  8       // max length of user DTMF input
#define respLen 10       // max length of voice response
#define serLen  40       // max length of serial string

Talkie voice; 

// Author's callsign, at time of writing:
uint8_t* CallSign[] = {spMIKE, spSEVEN, spKILO, spCHARLIE, spMIKE, spALTERNATE, 0};

unsigned long waitStart = millis();   // time of start to wait for next char
unsigned long flashStart = millis();  // time of last LED toggle
unsigned long openStart = millis();   // time of "opening" relay
unsigned long callStart = millis();   // time of last callsign
unsigned char newCh;                  // DTMF character
unsigned char DTMFpos;                // index into DTMF string
unsigned char serPos;                 // index into serial string
bool relayOpen;                       // flag that the relay is open
bool OnLED;                           // Flashing LED status
bool parse = false;                   // there is a received message to parse
bool addCallsign = true;              // callsign to be added
uint8_t* VoiceResponse[respLen];      // response to transmit touser
char serOut[serLen + 1];              // serial string to send including nul
char serIn[serLen + 1];               // MQTT string received including nul
char DTMFstr[DTMFlen + 1];            // read DTMF string including nul

// These are the characters we store when the given numbers are received by
// the MT8870 module. The info that 0 = D, for example, seems odd, but comes
// diretly from the data sheet. It is acknowledged that 11 and 12 should be
// "*" and "#" respectively, but we're trying to make hex numbers.
// char numStr[] = "D1234567890EFABC";
char numStr[] = "D1234567890*#ABC";

SoftwareSerial swSerial(swRx, swTx);

void setup()
{

  pinMode(Q1,  INPUT); // DTMF decoder pins
  pinMode(Q2,  INPUT);
  pinMode(Q3,  INPUT);
  pinMode(Q4,  INPUT);
  pinMode(STQ, INPUT);

  pinMode(PTT, OUTPUT);
  pinMode(LED, OUTPUT);

  pinMode(swRx, INPUT); // software serial port pin config
  pinMode(swTx, OUTPUT);  

  digitalWrite(PTT, LOW); // Turn PTT off

  Serial.begin(9600); // hardware (debug) serial

  swSerial.begin(9600); // software serial 

  Serial.println("");
  Serial.println("***RESET***");

} // setup

enum DTMFmode { DTMF_IDLE, DTMF_RX, DTMF_WAIT, DTMF_FULL, DTMF_END };
enum DTMFmode DTMFmode = DTMF_IDLE;

enum LEDMode { LED_OFF, LED_ON, LED_FLASH };
enum LEDMode LEDmode = LED_OFF;

enum serMode { SER_WAIT, SER_READ, SER_DONE };
enum serMode serMode = SER_WAIT;

void *buildDict(char *dest, const char *type, const char *data) { // build the output dictionary entry

  strcpy(dest,"{\"TYPE\":\"");   // move open dict into dest 
  strcat(dest,type);             // append type onto dest 
  strcat(dest,"\",\"DATA\":\""); // append separator to dest
  strcat(dest,data);             // append data to dest
  strcat(dest,"\"}");            // append close dict to type 

}

void SayString(uint8_t* speech[]) { // speak an array of arrays
  unsigned int i = 0;
  while (speech[i] != 0) {
    voice.say(speech[i]);
    i++;
  } // not done
} // SayString

void loop()
{
  switch (DTMFmode) {
    case DTMF_IDLE:
      if (digitalRead(STQ)) { // STQ has gone high
        DTMFpos = 0; // reset string pointer
        LEDmode = LED_FLASH; // indicates receiving
        DTMFmode = DTMF_RX;
        Serial.print("Rx: ");
      }
      break; // DTMF_IDLE

    case DTMF_RX:
      if (! digitalRead(STQ)) { // wait for STQ to go low
        waitStart = millis();

        // convert the IO bits into a decimal number then into a string
        newCh = ( digitalRead(Q1) | (digitalRead(Q2) << 1) | (digitalRead(Q3) << 2) | (digitalRead(Q4) << 3) );
        DTMFstr[DTMFpos] = numStr[newCh]; // look it up and store it

        Serial.print(DTMFstr[DTMFpos]);

        DTMFpos++; // increment string pointer

        if(DTMFpos >= DTMFlen) { // is input buffer full?
          DTMFmode = DTMF_FULL;
        } else {
          DTMFmode = DTMF_WAIT;
        }

      } // if STQ went low
      break; // DTMF_RX

    case DTMF_WAIT:
      if (millis() - waitStart > interTime) { // inter-character time expired
        LEDmode = LED_OFF;
        DTMFmode = DTMF_END;
        Serial.print(" TIMEOUT WITH: ");
      } else {
        if (digitalRead(STQ)) { DTMFmode = DTMF_RX; } // STQ has gone high
      }
      break; // DTMF_WAIT

    case DTMF_FULL:
      Serial.print(" BUFFER FULL WITH: ");
      DTMFmode = DTMF_END;
      break; // DTMF_FULL

    case DTMF_END:
      DTMFstr[DTMFpos] = 0; // terminator
      Serial.println(DTMFstr);
      parse = true;
      DTMFmode = DTMF_IDLE;
      LEDmode = LED_OFF; // indicates receiving
      break; // DTMF_END

  } // switch recMode

  if (parse) {
    parse = false; // mark as parsed
    Serial.print("PARSING... ");
    serOut[0] = 0;
    VoiceResponse[0] = 0;
    if (!strcmp(DTMFstr,openStr)) { // DTMF sequence for "open" received
      openStart = millis(); // note the time
      relayOpen = true;   // relay is open
      Serial.println("Relay OPEN");
      VoiceResponse[0] = spOPEN;
      VoiceResponse[1] = 0;
      buildDict(serOut,"STATUS","OPEN");
    } else if (!strcmp(DTMFstr, closeStr)) { // DTMF sequence for "close" received
      relayOpen = false;  // relay is closed
      Serial.println("Relay CLOSE");
      VoiceResponse[0] = spCLOSE;
      VoiceResponse[1] = 0;
      buildDict(serOut,"STATUS","CLOSED");
    } else {                              // some other DTMF sequence received
      if (relayOpen) {
        // the thing we do with messages goes here
        openStart = millis(); // note the time (assuming it was legit)
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" QSL (acknowledged)");
        buildDict(serOut,"DTMF",DTMFstr);
//        strcpy(serOut, DTMFstr); // REMEMBER destination, source
        VoiceResponse[0] = spQ; // QSL = acknowledge
        VoiceResponse[1] = spS;
        VoiceResponse[2] = spL;
        VoiceResponse[3] = 0;
      } else { // relay not open
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" NO QSL (not acknowledged)");
        serOut[0] = 0;
        VoiceResponse[0] = spNO; // No QSL = no acknowledge
        VoiceResponse[1] = spQ;
        VoiceResponse[2] = spS;
        VoiceResponse[3] = spL;
        VoiceResponse[4] = 0;
      } // relay not open

    } // switch DTMFstr  
  }

  if (relayOpen && millis() > openStart + closeTime) { // relay is open but has timed out
    relayOpen = false;   // relay is open
    Serial.println("Relay AUTO-CLOSE");
    buildDict(serOut,"STATUS","CLOSED");
    VoiceResponse[0] = spAUTOMATIC;
    VoiceResponse[1] = spCLOSE;
    VoiceResponse[2] = 0;
  } // timed out

  if (millis() > callStart + callTime) { // check last time callsign was added
    callStart = millis();
    if (!addCallsign) {
      Serial.println("CALLSIGN needs added");
    }
    addCallsign = true; // needs to be set false after being quqeued
  }

  if (millis() > flashStart + flashTime) { // is it time to toggle the flash?
    flashStart = millis();
    switch (LEDmode) { // refresh LED
      case LED_OFF:
        digitalWrite(LED, LOW);
        break;
      case LED_ON:
        digitalWrite(LED, HIGH);
        break;
      case LED_FLASH:
        OnLED = ! OnLED;
        digitalWrite(LED, OnLED);
        break;
    } // LEDmode    
  } // if FlashStart

  if (serOut[0] != 0) { // outgoing MQTT publication
    Serial.print("Serial Send: ");
    Serial.println(serOut);
    swSerial.print(serOut);
    serOut[0] = 0;
  }

  switch (serMode) {
    case SER_WAIT: // not receiving
      if (swSerial.available() > 0) { // incoming data is waiting
        serPos = 0;
        serMode = SER_READ;
      }
      break;
    case SER_READ: // receiving
      Serial.print("Serial read ");
      if (swSerial.available() > 0) { // keep receiving until buffer is empty
        serIn[serPos] = swSerial.read();
        if (serIn[serPos] == 10) { serIn[serPos] = 0; } // replace linefeed with EOL
        if (serPos < serLen) { serPos +=1; } // increment only if no overflow
      } else { // no more serial
        Serial.println("DONE");
        serMode = SER_DONE;
      }
    break;
    case SER_DONE: // not receiving, waiting for local buffer to be emptied
      if (serIn[0] == 0) {
        serMode = SER_WAIT;
      }
      break;
  }

  if (serIn[0] != 0) { // local buffer contains something
    Serial.print("Serial Received: [");
    Serial.print(serIn); // seems to terminate with linefeed...
    Serial.println("]");
    serIn[0] = 0; // start filling local buffer again
  }

  if (VoiceResponse[0] != 0) { // transmit the contents of VoiceResponse

    Serial.print("Tx VOICE ");
    digitalWrite(PTT, HIGH); // Turn PTT on
    digitalWrite(LED, HIGH); // Turn on LED

    delay(500); // pause before speaking

    // NB present logic is to only add the callsign IF there is a message to
    // send. The callsign might, alternatively, be broadcast on a regular
    // basis, but etiquette suggest it would be rude to blag a channel.

    if (addCallsign) { // add callsign as required
      addCallsign = false;
      SayString(CallSign);
      delay(500); // pause after callsign
    }

    SayString(VoiceResponse);
    digitalWrite(PTT, LOW); // Turn PTT on
    digitalWrite(LED, LOW); // Turn on LED
    VoiceResponse[0] = 0;
    delay(250); // brief pause before releasing button

    Serial.println("done");

  } // VoiceResponse is non-empty

  delay(10); // give the processor a wee break

} // loop
