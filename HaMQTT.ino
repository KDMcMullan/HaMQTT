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
/* Goertzel algorithm to work. I would really like to reduce my hardware
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
/* v0.95.00 20240626
/* Implemented Auto-close.
/* Now publishes an MQTT message on receipt of a DTMF sequence, if the relay
/* is open.
/* Periodically publishes an MQTT status message, and network information,
/* even if relay is closed.
/* Added in some QSO / no QSO counters just for sport.
/*
/* v0.95.01 20240708
/* Added a Timer class, since the timer code is a bit repetative; makes it
/* easier to manage.
/* Added automatic callsign broadcast on an infrequent basis, based on RSGB
/* recommendation.
/*
/* v0.95.02 20240708
/* Shortened some key values in MQTT dictionaries.
/* Moved the time client updater into the slowest loop.
/* Added certain user modifiable parameters to EEPROM.
/*
/****************************************************************************/
/*
/* To Do
/*
/* Make setup_wifi() and connect() non-blocking
/*  - Clearly, don't attempt connect() unil WiFi is connected
/*  - and don't attempt MQTT until connect() is sucessful.
/* ...or maybe don't bother with this? The system's pointless without WiFi.
/*
/* Add a web interface.
/* 
/* Allow EEPROM settings to be modified by radio, MQTT, or Web Interface.
/*
/* Add a command to query the time.
/*
/* Add an LED for RF Transmit (across PTT output?) and an LED for MQTT
/* traffic. Use built-in LED for RF Receive (as per persent setup). 
/*
/* Consider queueing outgoing mesages. They may take longer to transmit than
/* incoming commands / queries. (This looks like it may not be necessary as
/* the incoming MQTT seems to self-queue, and the production of speech seems
/* to be blocking. Hold that thought.)
/*
/* Add MQTT output key/value pairs for long timers such as:
/* - time before next auto-close
/* - time until next callsign broadcast
/* - time until next callsing prepend
/*
/* Optimise the pseudo-EEPROM. It's not exactly at a premium, but storing an
/* unsigned long of milliseconds is wasteful when an unsigned log of seconds
/* is almost a full day and of sufficient resolution.
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
#include <Preferences.h>

#include "AudioOutputI2SNoDAC.h"
#include "ESP8266SAM.h"

#include "kTimer.h"

// Complete Pin Ref: https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/

#define Q1 15   // D8, bit 0 input (boot fails if high)
#define Q2 13   // D7, bit 1 input (no special requirements)
#define Q3 4    // D2, bit 2 input (no special requirements)
#define Q4 14   // D5, bit 3 input (no special requirements)
#define STQ 16  // D0, steering input (high at boot. high output?)
#define PTT 12  // D6, push-to-talk via optocoupler (chosen for pull-down)

// #define AO  5         // Analog out = GPIO 5 = pin D1 on D1 mini

#define D1 5  // reserved for AO
#define D3 0  // (Avoid this: needs to be zero during flash)
#define D4 2  // (Avoid this: it's the built-in LED)

#define obLED LED_BUILTIN  // onboard LED

#define An 36  // from https://www.upesy.com/blogs/tutorials/measure-voltage-on-esp32-with-adc-with-arduino-code

// Tx GPIO1 debug output at boot, fails if pulled low
// Rx GPIO3

//--------------------------------------
// Factory Default Configs
//--------------------------------------

#define DTMFlen    8   // max length of user DTMF input
#define voiceLen 100   // max length (characters) of voice response
#define dictLen  200   // max length of published dictionary


//--------------------------------------
// Configuration Values
//--------------------------------------

#define flashTime       250  // ms of half period of flash (250ms = 2Hz)
#define charTime       3000  // ms between DTMF characters before timeout (3 sec)
#define preCallTime  120000  // ms between callsign prepend (2 min)
#define autoCallTime 600000  // ms between callsign prepend (10 min)


//--------------------------------------
// Configuration Values (EEPROM stored)
//--------------------------------------

unsigned long statTime   = 180000;  // ms between status MQTT broadcastt (3 min)
unsigned long openTime   = 300000;  // ms before auto-close (5 min)


//--------------------------------------
// Config (edit here before compiling)
//--------------------------------------

const char* ssid = "tweedledum";
const char* password = "jibber";

const char* mqtt_broker = "192.168.1.12";  // address of MQTT broker
uint16_t mqtt_broker_port = 1883;    // port number for MQTT broker
const char* mqttUser = "tweedledee";
const char* mqttPassword = "jabber";

const char* mqttTopicPub = "hamqtt/rx";     // publish things which were Rx
const char* mqttTopicSub = "hamqtt/tx";     // subscribe to things to Tx
const char* mqttTopicStat = "hamqtt/stat";  // publish status
const char* mqttTopicNet = "hamqtt/net";    // publish net stuff


//--------------------------------------
// DTMF / MQTT Engine Protocol Config
//--------------------------------------

#define closeStr "*700"  // DTMF sequence to close the dialog
#define openStr "*701"   // DTMF sequence to open the dialog


//--------------------------------------
// Language
//--------------------------------------

// NB the following are in something og a phonetic spelling as the English
// (UK) spellings of words did not always seem to pronouce well.

const char* MsgCallsign = "mike 7 keelo charlee mike";  // M7KCM
const char* MsgQSL = "ku ess ell";                      // No QSL
const char* MsgNoQSL = "no ku ess ell";                 // No QSL
const char* MsgRelayOpen = "reelay open";
const char* MsgRelayClosed = "reelay closed";
const char* MsgAutoClose = "reelay autto close";
const char* MsgChannelInUse = "channel, in yous";


//--------------------------------------
// Timer Objects
//--------------------------------------

kTimer TimerFlash(flashTime);
kTimer TimerChar(charTime);
kTimer TimerPreCall(preCallTime);
kTimer TimerStat(statTime);
kTimer TimerOpen(openTime);
kTimer TimerAutoCall(autoCallTime);


//--------------------------------------
// Other
//--------------------------------------

unsigned char newCh;                  // DTMF character
unsigned char DTMFpos;                // index into DTMF string
bool relayOpen = false;               // flag that the relay is open
bool OnLED;                           // Flashing LED status
bool parse = false;                   // there is a received message to parse
char DTMFstr[DTMFlen + 1];            // read DTMF string including nul
bool addCallsign = true;              // callsign to be added - true to use on first broadcast

char VoiceMsg[voiceLen];  // string to be spoken

char dict[dictLen];  // dictionary to be published

char* conv = (char*)malloc(20);  // globally allocated space for string conversions
char* localTime = (char*)malloc(20);

unsigned int CntQSL = 0;    // count of DTMFs in open mode
unsigned int CntNoQSL = 0;  // count of DTMFs in closed mode

enum DTMFmode { DTMF_IDLE,
                DTMF_RX,
                DTMF_WAIT,
                DTMF_FULL,
                DTMF_END };
enum DTMFmode DTMFmode = DTMF_IDLE;

enum LEDMode { LED_OFF,
               LED_ON,
               LED_FLASH };
enum LEDMode LEDmode = LED_OFF;

Preferences prefs; // EEPROM instance

// These are the characters we store when the given numbers are received by
// the MT8870 module. The order seems odd, but the fact that 0 = D, for
// comes directly from the data sheet.
char numStr[] = "D1234567890*#ABC";


//--------------------------------------
// globals derived from libraries
//--------------------------------------

WiFiClient wifiClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
PubSubClient mqttClient(wifiClient);
AudioOutputI2SNoDAC* out = NULL;


//--------------------------------------
// Press PTT, pause, say supplied string (prepended by callsing as required) then release.
//--------------------------------------

void transmit(const char* phrase, bool identify) {

  digitalWrite(PTT, HIGH);   // Turn PTT on
  digitalWrite(obLED, LOW);  // Turn on LED (no point using LEDmode as the next bit holds the process loop)
  delay(500);

  ESP8266SAM* sam = new ESP8266SAM;
  if (identify) {
    sam->Say(out, MsgCallsign);
    delay(250);
    addCallsign = false;  // don't like setting global this way - do it right
  }
  sam->Say(out, phrase);
  delete sam;

  delay(100);

  digitalWrite(PTT, LOW);
  //  digitalWrite(obLED, LOW); # this should happen automatically at the bottom of the loop

}  // transmit


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

  transmit("WiFi Connected", false);  // debug

}  // setup_wifi


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
  buffer[length] = '\0';  // Null terminate the buffer

  transmit(buffer, addCallsign);

}  // callback


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
      transmit("M Q T T connected", false);  // debug
  addCallsign = false;  // just sent it, no point sending it again.
    } else {
      Serial.print(".");
      //      Serial.print(". failed, rc=");
      //      Serial.print(mqttClient.state());
      //      Serial.println(" will try again in 5 seconds");
      delay(2500);
    }
  }
}  // connect

//--------------------------------------
// Convert integer to string
//--------------------------------------

char* int_str(int num) {
  sprintf(conv, "%d", num);
  return conv;
}  // int_str


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

}  // bool_str


//--------------------------------------
// Append key-value pair to a dictionary string
//--------------------------------------

void dictOpen(char* dest, const char* key, const char* value) {
  strcpy(dest, "{");  // start with {"
  dictAppend(dest, key, value);
}

void dictClose(char* dest) {
  dest[strlen(dest) - 1] = '\0';  // remove the last comma
  strcat(dest, "}");              // terminate with }"
}

void dictAppend(char* dest, const char* key, const char* value) {

  if (strlen(dest) + strlen(key) + strlen(value) + 6 < dictLen) {

    strcat(dest, "\"");     // append "      (1)
    strcat(dest, key);      // append key
    strcat(dest, "\":\"");  // append ":"    (+3 = 4)
    strcat(dest, value);    // append value
    strcat(dest, "\",");    // append ",     (+2 = 6)

  }  // if it fits

}  // dictUpdate


//--------------------------------------
// main arduino setup fuction called once
//--------------------------------------

void setup() {

  // This must come before the pinmode for Q1.
  // Something to do with the way the registers mess about with things.
  out = new AudioOutputI2SNoDAC();  // No DAC implementation uses Serial port Rx pin

  pinMode(Q1, INPUT);  // DTMF decoder pins
  pinMode(Q2, INPUT);
  pinMode(Q3, INPUT);
  pinMode(Q4, INPUT);
  pinMode(STQ, INPUT);

  pinMode(PTT, OUTPUT);
  pinMode(obLED, OUTPUT);

  digitalWrite(PTT, LOW);  // Turn PTT off

  Serial.begin(115200);

  transmit("Powerup complete", true);  // debug
  addCallsign = false; // just sent it, no point sending it again.

  setup_wifi();
  mqttClient.setServer(mqtt_broker, mqtt_broker_port);
  mqttClient.setCallback(callback);

  prefs.begin("HAM", false); // false means read/write mode

  if (prefs.getUChar("V", 0) == 0) { // namespace version is not as expected: add the keys
    prefs.putUChar("V", 0); // store EEPROM format version
    prefs.putString("CS",MsgCallsign);
    prefs.putString("ID",ssid);
    prefs.putString("PW",password);
    prefs.putString("MB",mqtt_broker);
    prefs.putUShort("MP",mqtt_broker_port);
    prefs.putString("MU",mqttUser);
    prefs.putString("MP",mqttPassword);
    prefs.putULong("ST",statTime);
    prefs.putULong("OT",openTime);
    Serial.println("Prefs not present.");

// I don't completely understand why this works, since there are defined as const.
// Perhaps it's teh adress of which is a const, therefore i have to be very careful with the lengths?

  } else {                           // read the keys from the namespace
    MsgCallsign =      prefs.getString("CS",MsgCallsign).c_str();
    ssid =             prefs.getString("ID",ssid).c_str();
    password =         prefs.getString("PW",password).c_str();
    mqtt_broker =      prefs.getString("MB",mqtt_broker).c_str();
    mqtt_broker_port = prefs.getUShort("MP",mqtt_broker_port);
    mqttUser =         prefs.getString("MU",mqttUser).c_str();
    mqttPassword =     prefs.getString("MP",mqttPassword).c_str();
    statTime =         prefs.getULong("ST",statTime);
    openTime =         prefs.getULong("OT",openTime);
    Serial.println("Used existing prefs.");
  }

}  // setup

//--------------------------------------
// main arduino loop fuction called continuously
//--------------------------------------

void loop() {
  if (!mqttClient.connected()) {
    connect();
  }

  switch (DTMFmode) {

    case DTMF_IDLE:
      if (digitalRead(STQ)) {  // STQ has gone high
        DTMFpos = 0;           // reset string pointer
        LEDmode = LED_FLASH;   // indicates receiving
        DTMFmode = DTMF_RX;
        Serial.print("Rx: ");
      }
      break;  // DTMF_IDLE

    case DTMF_RX:
      if (!digitalRead(STQ)) {  // wait for STQ to go low
        TimerChar.reset();

        // convert the IO bits into a decimal number then into a string
        newCh = (digitalRead(Q1) | (digitalRead(Q2) << 1) | (digitalRead(Q3) << 2) | (digitalRead(Q4) << 3));
        DTMFstr[DTMFpos] = numStr[newCh];  // look it up and store it

        Serial.print(DTMFstr[DTMFpos]);

        DTMFpos++;  // increment string pointer

        if (DTMFpos >= DTMFlen) {  // is input buffer full?
          DTMFmode = DTMF_FULL;
        } else {
          DTMFmode = DTMF_WAIT;
        }

      }       // if STQ went low
      break;  // DTMF_RX

    case DTMF_WAIT:
      if (TimerChar.expired()) {  // inter-character time expired
        LEDmode = LED_OFF;
        DTMFmode = DTMF_END;
        Serial.print(" TIMEOUT WITH: ");
      } else {
        if (digitalRead(STQ)) { DTMFmode = DTMF_RX; }  // STQ has gone high
      }
      break;  // DTMF_WAIT

    case DTMF_FULL:
      Serial.print(" BUFFER FULL WITH: ");
      DTMFmode = DTMF_END;
      break;  // DTMF_FULL

    case DTMF_END:
      DTMFstr[DTMFpos] = 0;  // terminator
      Serial.println(DTMFstr);
      parse = true;
      DTMFmode = DTMF_IDLE;
      LEDmode = LED_OFF;  // indicates waiting
      break;              // DTMF_END

  }  // switch DTMFmode

  if (parse) {
    parse = false;  // mark as parsed
    Serial.print("PARSING... ");
    if (!strcmp(DTMFstr, openStr)) {  // DTMF sequence for "open" received
      TimerOpen.reset();              // reset the open timer
      relayOpen = true;               // relay is open
      Serial.println("Relay OPEN");

      transmit(MsgRelayOpen, addCallsign);

    } else if (!strcmp(DTMFstr, closeStr)) {  // DTMF sequence for "close" received
      relayOpen = false;                      // relay is closed
      Serial.println("Relay CLOSE");

      transmit(MsgRelayClosed, addCallsign);

    } else {  // some other DTMF sequence received

      if (relayOpen) {
        // the thing we do with messages goes here
        TimerOpen.reset(); // reset the auto-close timer
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.print(" QSL (acknowledged): ");

        strncpy(localTime, timeClient.getFormattedTime().c_str(), 20);
        dictOpen(dict, "Time", localTime);
        dictAppend(dict, "DTMF", DTMFstr);
        dictClose(dict);
        Serial.println(dict);
        mqttClient.publish(mqttTopicPub, dict);  // publish the dictionary
        transmit(MsgQSL, addCallsign);           // Tx acknowledgement (actually maybe we won't?)

        CntQSL += 1;

      } else {  // relay not open
        Serial.print("MESSAGE: ");
        Serial.print(DTMFstr);
        Serial.println(" NO QSL (not acknowledged)");
        transmit(MsgNoQSL, addCallsign);

        CntNoQSL += 1;

      }  // relay not open

    }  // switch DTMFstr

  }  // if parse

  if (TimerStat.expired()) {  // check last time stat was published

    TimerStat.reset();
    
    strncpy(localTime, timeClient.getFormattedTime().c_str(), 20);
    dictOpen(dict, "Time", localTime);
    dictAppend(dict, "CS", MsgCallsign);
    dictAppend(dict, "Open", bool_str(relayOpen));
    dictAppend(dict, "AddCS", bool_str(addCallsign));
    dictAppend(dict, "QSLcnt", int_str(CntQSL));
    dictAppend(dict, "NoQSLcnt", int_str(CntNoQSL));
    dictAppend(dict, "Tchar", int_str(charTime));
    dictAppend(dict, "Topen", int_str(openTime));
    dictAppend(dict, "Tpre", int_str(preCallTime));
    dictAppend(dict, "Tauto", int_str(autoCallTime));
    dictClose(dict);
    mqttClient.publish(mqttTopicStat, dict);  // publish the dictionary

    dictOpen(dict, "SSID", ssid);
    dictAppend(dict, "BkrAdd", mqtt_broker);
    dictAppend(dict, "BkrPort", int_str(mqtt_broker_port));
    dictAppend(dict, "BkrUser", mqttUser);
    dictClose(dict);
    mqttClient.publish(mqttTopicNet, dict);  // publish the dictionary

  }  // stat has expired

  if (relayOpen && TimerOpen.expired()) {  // relay is open but has timed out
    relayOpen = false;                        // relay was open
    Serial.println("Relay AUTO-CLOSE");
    transmit(MsgAutoClose, addCallsign);

  }  // timed out

  if (TimerPreCall.expired()) {  // check last time callsign was added
    TimerPreCall.reset();
    TimerAutoCall.reset();    // don't need to announce station busy for a while yet
    if (!addCallsign) {
      Serial.println("CALLSIGN needs added");
    }
    addCallsign = true;  // needs to be set false after being quqeued
  }                      // callsign time has expired

  if (TimerAutoCall.expired()) {  // check last time callsign transmitted independently
    TimerAutoCall.reset();    // don't need to check until it expires again
    timeClient.update();      // this is only here because it's the slowest loop
    transmit(MsgChannelInUse, true); // send channel in use message, always with callsign
    Serial.println("Channel in use CALLSIGN sent");
  }                           // callsign time has expired

  if (TimerFlash.expired()) {  // is it time to toggle the flash?
    TimerFlash.reset();
    switch (LEDmode) {  // refresh LED (ESP8266 LED is active Low, so high = off)
      case LED_OFF:
        digitalWrite(obLED, HIGH);
        break;
      case LED_ON:
        digitalWrite(obLED, LOW);
        break;
      case LED_FLASH:
        OnLED = !OnLED;
        digitalWrite(obLED, OnLED);
        break;
    }  // LEDmode
  }    // if FlashStart

  mqttClient.loop();

}  // loop
