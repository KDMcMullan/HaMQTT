#!/usr/bin/python3
#
################################################################################
#
# HamMQTT Reprocessor
# - Sbscribes toa n MQTT publication from a Ham radio (Yes!)
# - Uses this data to publish an MQTT string which switches lights.
#
# Ken McMullan, M7KCM
#
################################################################################
#
# Don't forget to:
#   sudo apt install python-pip
#   pip install paho-mqtt 
#
################################################################################
#
# 10-Mar-2024
# A subset fork of my home MQTT post-processor.
# The MQTT string originates as a DTMF sequence sent by a ham radio, gets
# translated into an MQTT packet by TASMOTA, and published to a broker.
# Here, we subscribe to, and act on the MQTT data from the broker. For this
# basic demonstration, if the packet contains "data" and the data is "1" or
# "0", a packet is published which causes my studly light to be switched on or
# off as appropriate.
#
################################################################################
#
# Future
#
#
#
################################################################################
#
# Issues
#
#
#
################################################################################

# Consider setting teleperiod (in TASMOTA console) while debugging.

MQTTserver="192.168.1.12"
MQTTuser="blah"
MQTTpass="blah"

timerDisplayEnabled = False # no display output

logFileError = "/home/ken/python/mqttproc/hamerror.log"

subTopicHam = "hamqtt/tele/RESULT" # subscribe to this

myTopic = "hamqttproc" # in case I ned to publish a message from me


# This function causes PAHO errors to be logged to screen.
logToScreen = False # if False, PAHO callbacks do not display errors.

from time import sleep, monotonic
import sys
import subprocess
import datetime
import signal
import paho.mqtt.client as mqtt
import csv
import json

mqttc = mqtt.Client()
MQTTconnected = False # global in the context of on_connect and on_disconnect.

def compoundIf(bool, true, false):
  if (bool): ret = true
  else: ret = false
  return ret  

def stripLast(txt,delimiter=".",beg=0): # strips everything from and including the last instance of delimiter
  strtxt = str(txt) 
  return strtxt[:strtxt.rfind(delimiter,beg)]

def tryGet(payload, key, subkey = ""):
  try:
    ret = payload.get(key)
  except:
    ret = ""

  if subkey != "":
    try:
      ret = ret.get(subkey)
    except:
      ret = ""

  return ret

def politeExit(exitMsg):
  out = stripLast(datetime.datetime.now(),".")
  appendFile(logFileError, out + " " + exitMsg)
  mqttc.publish(myTopic + "/status", "Stopped")
  mqttc.will_set(myTopic + "/LWT", payload="AWOL")
  mqttc.loop_stop()
  print(exitMsg)

# store the original sigint and sigterm handlers
original_sigint = signal.getsignal(signal.SIGINT)
original_sigterm = signal.getsignal(signal.SIGTERM)

def signal_handler(incoming, frame):
  if (incoming == signal.SIGTERM):
    signal.signal(signal.SIGTERM, original_sigterm)
    politeExit("Received SIGTERM")
  if (incoming == signal.SIGINT):
    signal.signal(signal.SIGINT, original_sigint)
    politeExit("Ctrl-C pressed")
  sys.exit(0)

# set new handlers for ctrl-C and terminate
signal.signal(signal.SIGTERM, signal_handler)
signal.signal(signal.SIGINT, signal_handler)

def dictEntry(key,val,quoted=False):  
  return '"' + key + '":' + compoundIf(quoted,'"','') + str(val) + compoundIf(quoted,'"','') 

# The callback for when the client receives a CONNACK response from the MQTT broker.
def on_connect(client, userdata, flags, rc):
  global MQTTconnected
  MQTTconnected = True
  msg = "Connected with result code "+str(rc)
  msg = stripLast(datetime.datetime.now(),".") + " " + msg
  appendFile(logFileError, msg)
  print(msg)
  # Subscribing in on_connect() means that if we lose the connection and
  # reconnect then subscriptions will be renewed.
 
#  mqttc.subscribe([(subTopicHam,0),(...)])
  mqttc.subscribe([(subTopicHam,0)])

mqttc.on_connect = on_connect

# The callback for when the client diconnects from the broker (either politely, or impolitely).
def on_disconnect(client, userdata, flags, rc):
  global MQTTconnected
  MQTTconnected = False
#  if rc != 0: msg = "Unexpected disconnection."
  msg = "Disconnected with result code "+str(rc)
  msg = stripLast(datetime.datetime.now(),".") + " " + msg
  appendFile(logFileError, msg)
  print(msg)
 
mqttc.on_disconnect = on_disconnect

# The callback for when an unhandled PUBLISH message is received from the MQTT broker.
def on_message(client, userdata, msg):

  payload = json.loads(msg.payload.decode("utf-8"))

  print("on_message",str(payload))

mqttc.on_message = on_message

def callbackHam(client, userdata, msg):

  payload = json.loads(msg.payload.decode("utf-8"))
  received = payload.get("SerialReceived") # the code received from the TASMOTA relaying the DTMF
  mtype = received.get("TYPE") # record always contains TYPE and DATA definitions
  mdata = received.get("DATA")

#  print("DEBUG: ", mtype, mdata)

  match mtype:
    case "STATUS":
      print("Status:",mdata)
    case "DTMF":
      print("DTMF:",mdata)

      if mdata == "0":
        mqttc.publish("studylight/cmnd/power", "0")
      if mdata == "1":
        mqttc.publish("studylight/cmnd/power", "1")

    case _:
      print("SerialReceived.TYPE is unknown.")

mqttc.message_callback_add(subTopicHam, callbackHam)

# PAHO (from v1.4) doesn't raise errors; it logs them. This function causes them to be printed.
def on_log(mqttc, obj, level, string):
  print(string)

if logToScreen: mqttc.on_log = on_log

def appendFile(fname, txt):
  f = open(fname, 'a') # append
  f.write(f"{str(txt)}\n")
  f.close()

def formatTime(s):
  ss = int(s % 60)
  m = int((s - ss) / 60)
  mm = m % 60
  hh = int((m - mm) / 60)

  tStr = '{:02}'.format(hh) + ':' + '{:02}'.format(mm) + ':' + '{:02}'.format(ss)

  return tStr


########################################
# Main Program Starts Here
########################################

print("Starting...")


mqttc.will_set(myTopic + "/LWT", payload="Online")
mqttc.reconnect_delay_set(min_delay=2, max_delay=64)
mqttc.username_pw_set(MQTTuser, MQTTpass)

print("Running...")
print("Ctrl-C to exit")

while (True):

  if not MQTTconnected:
    try:
      mqttc.connect(MQTTserver, 1883, 60) # THIS WILL FAIL if the script runs before the MQTT server starts (eg at a reboot)
    except:
      print("Connection fail.")      
    else:
      mqttc.loop_start() # non-blocking call to manage network keep-alive, receiver, etc.

  sleep(1)
