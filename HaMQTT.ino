/****************************************************************************/
/*
/* HaMQTT (Ham Cue Tee Tee)
/* A DTMF / MQTT / Voice Relay for an Amateur Radio Station
/*
/* Ken McMullan
/* Inspired by WO4ROB
/* v0.91.05 20240303
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
/****************************************************************************/
/*
/* To Do
/*
/* add the ESP8266 hardware and the MQTT pub / sub.
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

//#include "string.h"
//#include "stdio.h"

#define Q1   8 // bit 0 input (direct from DTMF decoder)
#define Q2   7 // bit 1 input
#define Q3   6 // bit 2 input
#define Q4   5 // bit 3 input
#define STQ  4 // steering input

#define PTT  2 // PTT output (to PTT via optocoupler)

#define LED 13 // onboard LED

#define openStr  "E12A" // "*12A" DTMF sequence to open the relay
#define closeStr "FFFF" // "####" DTMF sequence to close the relay

#define closeTime 300000 // milliseconds before auto-close (5min)
#define callTime  120000 // milliseconds between callsign broadcast (2min)

#define interTime  3000 // time in ms allowed between characters before timeout
#define flashTime   250 // half rate in ms of flashing LED

#define strLen 8        // max length of user input

Talkie voice; 

unsigned long waitStart = millis();   // time of start to wait for next char
unsigned long flashStart = millis();  // time of last LED toggle
unsigned long openStart = millis();   // time of "opening" relay
unsigned long callStart = millis();   // time of last callsign
unsigned char newCh;                  // DTMF character
unsigned char strPos;                 // index into DTMF string
bool relayOpen;                       // flag that the relay is open
bool OnLED;                           // Flashing LED status
bool parse = false;                   // there is a received message to parse
bool addCallsign = true;              // callsign to be added
uint8_t* VoiceResponse[10];           // response to a reeived DTMF sequence
char MQTTresponse[10];                // response string
char DTMFstr[strLen +1];              // read DTMF string including nul
// Author's callsign, at time of writing:
uint8_t* CallSign[] = {spMIKE, spSEVEN, spKILO, spCHARLIE, spMIKE, spALTERNATE, 0};

// These are the characters we store when the given numbers are received by
// the MT8870 module. The info that 0 = D, for example, seems odd, but comes
// diretly from the data sheet. It is acknowledged that 11 and 12 should be
// "*" and "#" respectively, but we're trying to make hex numbers.
char numStr[] = "D1234567890EFABC";

void setup()
{

  pinMode(Q1,  INPUT);
  pinMode(Q2,  INPUT);
  pinMode(Q3,  INPUT);
  pinMode(Q4,  INPUT);
  pinMode(STQ, INPUT);

  pinMode(PTT, OUTPUT);
  pinMode(LED, OUTPUT);
  
  Serial.begin(9600);
  Serial.println("");
  Serial.println("***RESET***");

} // setup

enum DTMFmode { DTMF_IDLE, DTMF_RX, DTMF_WAIT, DTMF_FULL, DTMF_END };
enum LEDMode { LED_OFF, LED_ON, LED_FLASH };

enum DTMFmode DTMFmode = DTMF_IDLE;
enum LEDMode LEDmode = LED_OFF;

void SayString(uint8_t* speech[]) {
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
        strPos = 0; // reset string pointer
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
        DTMFstr[strPos] = numStr[newCh]; // look it up and store it

        Serial.print(DTMFstr[strPos]);

        strPos++; // increment string pointer

        if(strPos >= strLen) { // is input buffer full?
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
      DTMFstr[strPos] = 0; // terminator
      Serial.println(DTMFstr);
      parse = true;
      DTMFmode = DTMF_IDLE;
      LEDmode = LED_OFF; // indicates receiving
      break; // DTMF_END

  } // switch recMode

  if (parse) {
    parse = false; // mark as parsed
    Serial.print("PARSING... ");
    strcpy("",MQTTresponse);
    VoiceResponse[0] = 0;
    if (!strcmp(DTMFstr,openStr)) {
      openStart = millis(); // note the time
      relayOpen = true;   // relay is open
      Serial.println("Relay OPEN");
      VoiceResponse[0] = spOPEN;
      VoiceResponse[1] = 0;
      strcpy("OPEN",MQTTresponse);
    } else if (!strcmp(DTMFstr, closeStr)) {
      relayOpen = false;  // relay is closed
      Serial.println("Relay CLOSE");
      VoiceResponse[0] = spCLOSE;
      VoiceResponse[1] = 0;
      strcpy("CLOSED",MQTTresponse);
    } else {
      if (relayOpen) {
        // the thing we do with messages goes here
        openStart = millis(); // note the time (assuming it was legit)
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" QSL (acknowledged)");
        strcpy(DTMFstr,MQTTresponse);
        VoiceResponse[0] = spQ; // QSL = acknowledge
        VoiceResponse[1] = spS;
        VoiceResponse[2] = spL;
        VoiceResponse[3] = 0;
      } else { // relay not open
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" NO QSL (not acknowledged)");
        strcpy("",MQTTresponse);
        VoiceResponse[0] = spNO; // No QSL = no acknowledge
        VoiceResponse[1] = spQ;
        VoiceResponse[2] = spS;
        VoiceResponse[3] = spL;
        VoiceResponse[4] = 0;
      } // relay not open

    } // switch DTMFstr  
  }

  if (relayOpen) { // if relay is open
    if (millis() > openStart + closeTime) { // and it has timed out
      relayOpen = false;   // relay is open
      Serial.println("Relay AUTO-CLOSE");
      VoiceResponse[0] = spAUTOMATIC;
      VoiceResponse[1] = spCLOSE;
      VoiceResponse[2] = 0;
    } // timed out

    // other things to do if it is open bt not timed out, otherwise, compound the logic

  }

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

  if (VoiceResponse[0] != 0) {
    Serial.print("Tx VOICE ");
    digitalWrite(PTT, HIGH); // Turn PTT on
    digitalWrite(LED, HIGH); // Turn on LED
    // transmit the contents of VoiceResponse[] 
    // if it's a callsign, don't forget to add "spALTERNATE" 
    // implement periodic callsign Tx here
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
    Serial.println("done");
  } // VoiceResponse is non-empty

  delay(100); // give the processor a wee break

} // loop
