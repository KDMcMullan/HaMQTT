/****************************************************************************/
/*
/* HaMQTT (Ham Cue Tee Tee)
/* A DTMF / MQTT / Voice Relay for an Amateur Radio Station
/*
/* Ken McMullan M7KCM
/*
/****************************************************************************/
/*
/* History
/*
/* Having been inspired by something I saw by Ham enthusiast WO4ROB, I set
/* about taking his work a little further. This was largely developed as a
/* simple Arduino program. It received radio input to a MT8870 DTMF decoder,
/* whose output was used to send serial port data to a TASMOTA-flashed
/* ESP8266. TASMOTA then sent the data onward to an MQTT broker, and those
/* packets were used to control lights. The arduino was able to respond, via
/* the radio transceiver using a 1980's Speak 'n' Spell speech synthesiser to
/* update the user. This development was in February / March of 2024.
/* In March, I decided to remove the Arduino, the MT8770, and TASMOTA, and
/* try to make everything happen on the ESP8266. I could not get PhoneDTMF
/* to work and restored the MT8770. Several folk claim to have got the
/* Goertzel algorithm to work. I would really like to reducemy hardware
/* footprint, but for now I have MQTT working and I'm focussing on getting
/* audio response back out of the system. I'll have another go at some point.
/* (See also https://github.com/Estylos/PhoneDTMF/ .)

/* v0.94.11 20240509
/* MQTT functioning on ESP8288 (D1 Mini), using MT8870.
/*
/* v0.94.12 20240610
/* This has some debug code to generate beeps. Next version will remove that.
/*
/* v0.94.13 20240616
/* Working implementation of https://github.com/earlephilhower/ESP8266Audio.
/*
/* v0.94.14 20240624
/* Better implementation of ESP8266Audio.
/* Demonstrates ability to output a received MQTT string. (A string message
/* published to topic mqtt/tx will be transmitted by the radio.)
/* Breaking change: Start of implementation of input (command / query)
/* protocol. (*701 and *700 used to open and close communications.)
/* Groundwork started on publishing MQTT as a python-style dictionary.
/*
/****************************************************************************/
/*
/* To Do
/*
/* Implement auto-close.
/*
/* Make setup_wifi() and connect() non-blocking
/*  - Clearly, don't attempt connect() unil WiFi is connected
/*  - and don't attempt MQTT until connect() is sucessful.
/*
/* Once protocol supports local settings, store timeouts, etc in EEPROM.
/*
/* Consider queueing outgoing mesages. They may take longer to transmit than
/* incoming commands / queries.
/*
/****************************************************************************/
/*
/* Notes:
/*
/* Don't forget that the connected radio must have the volume up. A setting
/* between 1/2 and 3/4 is suggested.
/*
/****************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "AudioOutputI2SNoDAC.h"
#include "ESP8266SAM.h"

// Complete Pin Ref: https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

#define Q1  15 // D8, bit 0 input (boot fails if high)
#define Q2  13 // D7, bit 1 input
#define Q3   4 // D2, bit 2 input
#define Q4  14 // D5, bit 3 input
#define STQ 16 // D0, steering input
#define PTT 12 // D6, push-to-talk via optocoupler (chosen for pull-down)

// #define AO  5         // Analog out = GPIO 5 = pin D1 on D1 mini

#define D1   5 // reserved for AO
#define D3   0 // (Avoid this: needs to be zero during flash)
#define D4   2 // (Avoid this: it's the built-in LED)

#define obLED LED_BUILTIN // onboard LED

#define An 36 // from https://www.upesy.com/blogs/tutorials/measure-voltage-on-esp32-with-adc-with-arduino-code

//--------------------------------------
// Factory Default Configs
//--------------------------------------

#define DTMFlen       8 // max length of user DTMF input
#define voiceLen    100 // max length (characters) of voice response
#define flashTime   250 // half rate in ms of flashing LED

//--------------------------------------
// DTMF Modifiable Configs (EEPROM Stored)
//--------------------------------------

#define interTime  3000 // time in ms allowed between characters before timeout
#define closeTime 300000 // milliseconds before auto-close (5min)
#define callTime  120000 // milliseconds between callsign broadcast (2min)

//--------------------------------------
// DTMF / MQTT Engine Protocol Config
//--------------------------------------

#define closeStr "*700" // DTMF sequence to close the dialog
#define openStr  "*701" // DTMF sequence to open the dialog

//--------------------------------------
// Config (edit here before compiling)
//--------------------------------------

const char* ssid = "Scrabo";
const char* password = "somethingspecial";
const char* mqtt_broker = "192.168.1.12"; // address of MQTT broker
const uint16_t mqtt_broker_port = 1883; // port number for MQTT broker
const char* mqttUser = "device";
const char* mqttPassword = "equallyspecial";
const char* mqttTopicPub = "hamqtt/rx";    // publish things which were Rx
const char* mqttTopicSub = "hamqtt/tx";    // subscribe to things to Tx
const char* mqttTopicStat = "hamqtt/stat"; // publish status

//--------------------------------------
// Language
//--------------------------------------

const char* MsgCallsign = "mike 7 keelo charlee mike"; // M7KCM
const char* MsgQSL = "ku ess ell"; // No QSL
const char* MsgNoQSL = "no ku ess ell"; // No QSL
const char* MsgRelayOpen = "reelay open";
const char* MsgRelayClosed = "reelay closed";

//--------------------------------------
// Other
//--------------------------------------

unsigned long waitStart = millis();   // time of start to wait for next char
unsigned long flashStart = millis();  // time of last LED toggle
unsigned long openStart = millis();   // time of "opening" relay
unsigned long callStart = millis();   // time of last callsign
unsigned char newCh;                  // DTMF character
unsigned char DTMFpos;                // index into DTMF string
bool relayOpen = false;               // flag that the relay is open
bool OnLED;                           // Flashing LED status
bool parse = false;                   // there is a received message to parse
char DTMFstr[DTMFlen + 1];            // read DTMF string including nul
bool addCallsign = true;              // callsign to be added - true to use on first broadcast 

char VoiceMsg[voiceLen];              // string to be spoken

char *pubDict = NULL;                 // dictionary to be published on Rx
char *statDict = NULL;                // dictionary to be published periodically

char *conv = (char *)malloc(20);      // globally allocated space for string conversions

enum DTMFmode { DTMF_IDLE, DTMF_RX, DTMF_WAIT, DTMF_FULL, DTMF_END };
enum DTMFmode DTMFmode = DTMF_IDLE;

enum LEDMode { LED_OFF, LED_ON, LED_FLASH };
enum LEDMode LEDmode = LED_OFF;

// These are the characters we store when the given numbers are received by
// the MT8870 module. The order seems odd, but the fact that 0 = D, for
// comes directly from the data sheet.
char numStr[] = "D1234567890*#ABC";


//--------------------------------------
// globals
//--------------------------------------

WiFiClient wifiClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
PubSubClient mqttClient(wifiClient);
AudioOutputI2SNoDAC *out = NULL;


//--------------------------------------
// function setup_wifi called once
//--------------------------------------

void setup_wifi() {

  delay(10);
  Serial.println();
  Serial.print("WiFi connecting to ");
  Serial.print(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  timeClient.begin();

  Serial.println("connected");

} // setup_wifi


//--------------------------------------
// function callback called everytime an 
// mqtt message arrives from the broker
//--------------------------------------

void callback(char* topic, byte* payload, unsigned int length) {

  char buffer[length + 1];

  Serial.print("Message arrived on topic: '");
  Serial.print(topic);
  Serial.print("' with payload: ");
  for (unsigned int i = 0; i < length; i++) {
    buffer[i] = (char)payload[i];
    Serial.print((char)payload[i]);
  }
  Serial.println();
  buffer[length] = '\0'; // Null terminate the buffer

  transmit(buffer,addCallsign);
  
} // callback


//--------------------------------------
// function connect called to (re)connect
// to the broker
//--------------------------------------

void connect() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT broker: ");
    Serial.print(mqtt_broker);
    String mqttClientId = "";
    if (mqttClient.connect(mqttClientId.c_str(), mqttUser, mqttPassword)) {
      Serial.println(" connected");
      mqttClient.subscribe(mqttTopicSub);
//      String myCurrentTime = timeClient.getFormattedTime();
//      mqttClient.publish(mqttTopicPub,("HaMQTT connected. Time: " + myCurrentTime).c_str());
      mqttClient.publish(mqttTopicPub,"HaMQTT connected.");
    } else {
      Serial.print(".");
//      Serial.print(". failed, rc=");
//      Serial.print(mqttClient.state());
//      Serial.println(" will try again in 5 seconds");
      delay(2500);
    }
  }
} // connect


//--------------------------------------
// Press PTT, pause, say supplied string (prepended by callsing as required) then release.
//--------------------------------------

void transmit(const char* phrase, bool identify) {

  digitalWrite(PTT, HIGH); // Turn PTT on
  digitalWrite(obLED, HIGH); // Turn on LED (no point using LEDmode as the next bit holds the process loop)
  delay(500);

  ESP8266SAM *sam = new ESP8266SAM;
  if (identify) {
    sam->Say(out, MsgCallsign);
    delay(250);
    addCallsign = false; // don't like setting global this way - do it right
  }  
  sam->Say(out, phrase);
  delete sam;

  delay(250);

  digitalWrite(PTT, LOW);
  digitalWrite(obLED, LOW);

} // transmit


//--------------------------------------
// Convert integer to string
//--------------------------------------

char* int_str(int num) {
  sprintf(conv, "%d", num);
  return conv;
} // int_str


//--------------------------------------
// Convert boolean-like integer to string
//--------------------------------------

char* bool_str(int b) {

  if (b) {
    strcpy(conv, "true");
  } else {
    strcpy(conv, "false");
  }
  
  return conv;

} // bool_str


//--------------------------------------
// Append or overwrite key-value pair in a dictionary string
//--------------------------------------

void dictUpdate(char **dict_str, const char *key, const char *value) {

 // ISSUES: remove the first "}"
 // ISSUES: remove the "}" after every key/value pair
 // ISSUES: remove the first ","
 // ISSUES: put the key and value in double-quotes

  if (*dict_str == NULL) { // Initialise the dictionary string if it's NULL
    *dict_str = (char *)malloc(3); // "{}\0"
    strcpy(*dict_str, "{}");
  }

  char *key_start = strstr(*dict_str, key);

  // Check if the key already exists in the dictionary
  if (key_start != NULL) { // Key exists, find its value and replace it
    char *value_start = strchr(key_start, ':') + 2; // move past ": "
    char *comma_pos = strchr(value_start, ',');
    if (comma_pos != NULL) { // Key is in the middle of the dictionary
      int len = comma_pos - value_start;
      strncpy(value_start, value, len);
      value_start[len] = '\0';
    } else { // Key is the last entry in the dictionary
      strcpy(value_start, value);
      strcat(*dict_str, "}");
    } // comma position is null
  } else { // Key does not exist, add new key-value pair
    int old_len = strlen(*dict_str);
    int key_len = strlen(key);
    int value_len = strlen(value);
    *dict_str = (char *)realloc(*dict_str, old_len + key_len + value_len + 7); // +7 for ", key: value }"
    strcat(*dict_str, ", ");
    strcat(*dict_str, key);
    strcat(*dict_str, ": ");
    strcat(*dict_str, value);
    strcat(*dict_str, "}");
  } // key start is null

} // dictUpdate


//--------------------------------------
// main arduino setup fuction called once
//--------------------------------------

void setup() {

  // This must come before the pinmode for Q1.
  // Something to do with the way the registers mess about with things.
  out = new AudioOutputI2SNoDAC(); // No DAC implementation uses Serial port Rx pin

  pinMode(Q1,  INPUT); // DTMF decoder pins
  pinMode(Q2,  INPUT);
  pinMode(Q3,  INPUT);
  pinMode(Q4,  INPUT);
  pinMode(STQ, INPUT);

  pinMode(PTT, OUTPUT);
  pinMode(obLED, OUTPUT);

  digitalWrite(PTT, LOW); // Turn PTT off

  Serial.begin(115200);

  setup_wifi();
  mqttClient.setServer(mqtt_broker, mqtt_broker_port);
  mqttClient.setCallback(callback);

  transmit("Powerup complete", true); // debug

//  dictUpdate(&statDict, "InputTime", int_str(interTime));
//  dictUpdate(&statDict, "CloseTime", int_str(closeTime));
//  dictUpdate(&statDict, "CallsignTime", int_str(callTime));
  dictUpdate(&statDict, "RelayOpen", bool_str(relayOpen));
  dictUpdate(&statDict, "Callsign", MsgCallsign);
  dictUpdate(&statDict, "SSID", ssid);
  dictUpdate(&statDict, "Broker", mqtt_broker);
  Serial.print(statDict);

  mqttClient.publish(mqttTopicStat,statDict); // publish the dictionary

} // setup

//--------------------------------------
// main arduino loop fuction called continuously
//--------------------------------------

void loop() {
  if (!mqttClient.connected()) {
    connect();
  }
 
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
      LEDmode = LED_OFF; // indicates waiting
      break; // DTMF_END

  } // switch DTMFmode

  if (parse) {
    parse = false; // mark as parsed
    Serial.print("PARSING... ");
    if (!strcmp(DTMFstr,openStr)) { // DTMF sequence for "open" received
      openStart = millis(); // note the time
      relayOpen = true;   // relay is open
      Serial.println("Relay OPEN");

      transmit(MsgRelayOpen,addCallsign);

    } else if (!strcmp(DTMFstr, closeStr)) { // DTMF sequence for "close" received
      relayOpen = false;  // relay is closed
      Serial.println("Relay CLOSE");

      transmit(MsgRelayClosed,addCallsign);

    } else {                              // some other DTMF sequence received
      if (relayOpen) {
        // the thing we do with messages goes here
        openStart = millis(); // note the time (assuming it was legit)
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" QSL (acknowledged)");

        dictUpdate(&pubDict, "DTMF", DTMFstr);
        mqttClient.publish(mqttTopicPub,pubDict); // publish the dictionary

//        mqttClient.publish(mqttTopicPub,DTMFstr); // publish the DTMF sequence
        transmit(MsgQSL,addCallsign); // Tx acknowledgement (actually maybe we won't?)
        
      } else { // relay not open
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" NO QSL (not acknowledged)");

        transmit(MsgNoQSL,addCallsign);

      } // relay not open

    } // switch DTMFstr  

    dictUpdate(&statDict, "Open", bool_str(relayOpen));

  } // if parse

  if (millis() > callStart + callTime) { // check last time callsign was added
    callStart = millis();
    if (!addCallsign) {
      Serial.println("CALLSIGN needs added");
    }
    addCallsign = true; // needs to be set false after being quqeued
  }

  if (millis() > flashStart + flashTime) { // is it time to toggle the flash?
    flashStart = millis();
    switch (LEDmode) { // refresh LED (ESP8266 LED is active Low, so high = off)
      case LED_OFF:
        digitalWrite(obLED, HIGH);
        break;
      case LED_ON:
        digitalWrite(obLED, LOW);
        break;
      case LED_FLASH:
        OnLED = ! OnLED;
        digitalWrite(obLED, OnLED);
        break;
    } // LEDmode    
  } // if FlashStart

  mqttClient.loop();
  timeClient.update(); // this probably doesn't need done EVERY loop

} // loop
