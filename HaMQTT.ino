/****************************************************************************/
/*
/* HaMQTT (Ham Cue Tee Tee)
/* A DTMF / MQTT / Voice Relay for an Amateur Radio Station
/*
/* Ken McMullan
/* Inspired by WO4ROB
/* v0.90.04 20240227
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
/****************************************************************************/
/*
/* To Do
/*
/* Add the voice response to received sequences.
/*
/* add the ESP8266 hardware and the MQTT pub / sub.
/*
/* Why does Arduino loose USB connection when firmware keys the PTT?
/*
/****************************************************************************/
/*
/* Notes:
/*
/* Don't forget that the connected radio has to have the volume up. A setting
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

#define closeTime 180000 // milliseconds before auto-close (3 minutes)
#define callTime   60000 // milliseconds between callsign broadcast (1 minute)

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
bool addCallsign;                     // callsign to be added
char VoiceResponse[10];               // response string
char MQTTresponse[10];                // response string
char DTMFstr[strLen +1];              // read DTMF string including nul
char utterance;                       // next thing to be said

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
    strcpy("",VoiceResponse);
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
        Serial.println(" acknowledged");
        strcpy(DTMFstr,MQTTresponse);
        VoiceResponse[0] = spQ; // QSL = acknowledge
        VoiceResponse[1] = spS;
        VoiceResponse[2] = spL;
        VoiceResponse[0] = 0;
      } else { // relay not open
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" not acknowledged");
        strcpy("",MQTTresponse);
        VoiceResponse[0] = spNO; // No QSL = no acknowledge
        VoiceResponse[1] = spQ;
        VoiceResponse[2] = spS;
        VoiceResponse[3] = spL;
        VoiceResponse[4] = 0;
      } // relay not open

    } // switch DTMFstr  
  }

  if (relayOpen) { // timeout open relay
    if (millis() > openStart + closeTime) {
      relayOpen = false;   // relay is open
      Serial.println("Relay AUTO-CLOSE");
      VoiceResponse[0] = spAUTOMATIC;
      VoiceResponse[1] = spCLOSE;
      VoiceResponse[2] = 0;
    } // timed out
  }

  if (millis() > callStart + callTime) { // check last time callsign was added
    callStart = millis();
    addCallsign = true; // needs to be set false after being quqeued
//    Serial.println("Adding CALLSIGN");
  }

  if (millis() > flashStart + flashTime) { // is it time totoggle the flash?
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

  if (false) {

    Serial.print("Txing... ");

    digitalWrite(PTT, HIGH); // Turn PTT on
    digitalWrite(LED, HIGH); // Turn on LED

    // transmit the contents of VoiceResponse[] 
    // if it's a callsign, don't forget to add "spALTERNATE" 

    digitalWrite(PTT, LOW); // Turn PTT on
    digitalWrite(LED, LOW); // Turn on LED

    Serial.println(" DONE");

  }

  delay(100); // give the processor a wee break

} // loop
