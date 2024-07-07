#!/usr/bin/python3
#
################################################################################
#
# HamMQTT Reprocessor
# - Sbscribes to an MQTT publication from a Ham radio (Yes!)
# - Uses this data to publish an MQTT string which switches lights.
# - Can also publish a text to the transmitter by way of response to queries.
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
# 23-Jun-2026
# The program subscribes to an MQTT topic, which contains a DTMF sequence. The
# sequence is nominally sourced from a device like a phone or a ham radio. If
# the sequences starts with "*" it's a command; if it starts "#", it's a query.
# A list of recognised sequences "cmds" is stored, along with the actions upon
# receipt.
# If the DTMF sequence is recognised a particular MQTT topic and data are
# published. Additionally, an MQTT topic is subscribed to, which  would contain
# a response. A JSON string indicates where to find the response within the
# topic.
#
################################################################################
#
# Future
#
# Allow the creation of a list of stuff to monitor. So (eg) each time the
# bathroom light changes, the state is always transmitted, regardless where the
# command came from.
#
# Create a webserver, which allows the creation / editing of the dictionary of
# commands / responses.
#
# Modify the hardware / softwre to allow querying and setting of specific
# features of the radio transceiver, eg output power, received signal strength,
# bettery level.
#
################################################################################
#
# Issues
#
# Need to UNSUBSCRIBE once the message has been received, as some MQTT status
# messaegs contain multiple sensors and all sensors previously queued will be
# returned.
#
################################################################################
#
# Record Structure:
#
# Ddtmf: the DTMF string for comparison agains the strings received.
# Ddesc: a (phoneticised) spoken language interpretation of the sensor.
# Dtopic: the MQTT topic to be published upon reecipt of "Ddtmf" string.
# Ddata: the data to be published on teh above topic.
# DrespTopic: the topic to be subscribed to, which contains the response.
# DrespKey: the JSON pinpointing the response.
#
################################################################################

MQTTserver="192.168.1.12"
MQTTuser="device"
MQTTpass="equallyspecial"

# declaring these as constants should reduce the possibility of human error in creating the dictionary

Ddtmf =  "dtmf"     # DTMF sequence from transmitter
Ddesc =  "desc"     # spoken description
Dtopic = "Dtopic"   # MQTT command
Ddata =  "Ddata"    # MQTT command data 
DrespTopic =  "DtopicR" # MQTT response topic
DrespKey =  "Dkey" # MQTT response key

cmds=[
  {Ddtmf:"#100",  Ddesc:"outsied temperature",       Dtopic:"garageweather/cmnd/status", Ddata:"8",
                  DrespTopic: "garageweather/stat/STATUS8", DrespKey:"StatusSNS.SI7021-14.Temperature"},

  {Ddtmf:"#101",  Ddesc:"downstairs temperature",    Dtopic:"cloakroom/cmnd/status",     Ddata:"8",
                  DrespTopic: "cloakroom/stat/STATUS8",     DrespKey:"StatusSNS.SI7021.Temperature"},

  {Ddtmf:"#102",  Ddesc:"up stairs temperature",      Dtopic:"hottank/cmnd/status",       Ddata:"8",
                  DrespTopic: "hottank/stat/STATUS8",       DrespKey:"StatusSNS.SI7021.Temperature"},

  {Ddtmf:"#200",  Ddesc:"bath rume, mane light",     Dtopic:"ch4_01/cmnd/status",        Ddata:"11",
                  DrespTopic: "ch4_01/stat/STATUS11",        DrespKey:"StatusSTS.POWER1"},

  {Ddtmf:"#201",  Ddesc:"bath room, merror light",     Dtopic:"ch4_01/cmnd/status",        Ddata:"11",
                  DrespTopic: "ch4_01/stat/STATUS11",        DrespKey:"StatusSTS.POWER2"},

  {Ddtmf:"*2000", Ddesc:"bath rume, mane light, off",   Dtopic:"ch4_01/cmnd/POWER1",     Ddata:"off"},

  {Ddtmf:"*2001", Ddesc:"bath rume, mane light, on",    Dtopic:"ch4_01/cmnd/POWER1",     Ddata:"on"},

  {Ddtmf:"*2010", Ddesc:"bath rume, merror light off", Dtopic:"ch4_01/cmnd/POWER2",     Ddata:"off"},

  {Ddtmf:"*2011", Ddesc:"bath rume, merror light on",  Dtopic:"ch4_01/cmnd/POWER2",    Ddata:"on"}

] # commands from the transmitter

searches=[] # list of response topics for which we are waiting
subList=[] # list of topics to which we've subscribed

timerDisplayEnabled = False # no display output

logFileError = "/home/ken/python/hamqtt/hamerror.log"

baseTopic = "hamqtt"
subTopicStat = baseTopic + "/stat" # subscribe to this
subTopicNet = baseTopic + "/net"   # subscribe to this
subTopicRx = baseTopic + "/rx"     # subscribe to this

subTopicTx = baseTopic + "/tx"     # to publish a message from me

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

def getNestedDictKey(dict, dotted_key): # Function to get value using dotted string
  ret = dict
  try:
    for key in dotted_key.split('.'):
      ret = value[key]
  except:
    ret = ""
 return ret
    
def politeExit(exitMsg):
  out = stripLast(datetime.datetime.now(),".")
  appendFile(logFileError, out + " " + exitMsg)
  mqttc.publish(baseTopic + "/status", "Stopped")
  mqttc.will_set(baseTopic + "/LWT", payload="AWOL")
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
  mqttc.subscribe([(subTopicRx,0),(subTopicStat,0),(subTopicNet,0)])

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

# The callback for when a subscribed, topic with no sspecific callback message is received
def on_message(client, userdata, msg):

  topic = msg.topic
  payload = json.loads(msg.payload.decode("utf-8"))

  found = False

# IF the topic is sought, extract the portion of the payload to return (speak) its value.

  for resp in searches:
    if topic == resp[DrespTopic]: # if the value of the response topic equals the received topic
      found = True
      result = getNestedDictKey(payload,resp[DrespKey])

      print(f"EXTRACTED: {resp[DrespKey]} from {topic} as {result}")
      mqttc.publish(subTopicTx, f"{resp[Ddesc]} {result}") # issue the response to the radio transmitter
      print(f"SAY: {resp[Ddesc]} {result}")

  if found:
    print("Attended: ",end="")
  else:
    print("Unattended: ",end="")
  print(f"{str(topic)}: {str(payload)}")
 
mqttc.on_message = on_message

def callbackRx(client, userdata, msg):

  payload = json.loads(msg.payload.decode("utf-8"))

  time = payload.get("Time") # record always contains TYPE and DATA definitions
  DTMF = payload.get("DTMF")

  print(f"{stripLast(datetime.datetime.now())} ", end="")
  print("DEBUG: ", DTMF)

  found = False
  for cmd in cmds:
    if cmd[Ddtmf] == DTMF: # if the value of the DTMF key equals the received DTMF sequence
      found = True

      # Initial thoughts were that there's a better way to do this.
      # Regardless if it's a set or a query, both consist of a command and a response:
      #   simply publish the command and subscribe to / act on the response.
      # Conversely, it may actually have to get more complicated:
      #   if we want to set a value other than 1 or 0, we'll need to further parse the DTMF string.  

      if cmd[Ddtmf][0] == "*": # set
        print(f"SET: {cmd[Dtopic]} {cmd[Ddata]} \"{cmd[Ddesc]}\"")
        mqttc.publish(cmd[Dtopic], cmd[Ddata]) # issue the command to the remote device
        mqttc.publish(subTopicTx, cmd[Ddesc]) # issue the response to the radio transmitter

      if cmd[Ddtmf][0] == "#": # query
        print(f"QUERY: {cmd[Dtopic]} {cmd[Ddata]} \"{cmd[Ddesc]}\"")
        print(f"EXPECTING: {cmd[DrespTopic]} {cmd[DrespKey]}")
        searches.append(cmd) # load the response search list
        
#        mqttc.publish(subTopicTx, "wilco") # respond to sender that the request is in progress
        mqttc.publish(cmd[Dtopic], cmd[Ddata]) # provoke the response
        print(f"SEARCHES: {searches}")

# WE SHOULD UNSUBSCRIBE once the message has been received, as some MQTT status messaegs contain
# multiple sensors and all sensors previously queued will be returned.

        if cmd[DrespTopic] not in subList: # command response has not yet been subscribed
          subList.append(cmd[DrespTopic])
          mqttc.subscribe(cmd[DrespTopic])
#          print(f"New subscription: {cmd[DrespTopic]}")

  if not found:
    print(f"DTMF {DTMF} unrecognised.")
    mqttc.publish(subTopicTx, DTMF + " not recognised") # issue the response to the radio transmitter

mqttc.message_callback_add(subTopicRx, callbackRx)

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

def plurals(n):
  return compoundIf(n == 1,"","s")

  
########################################
# Main Program Starts Here
########################################

print("Starting...")

# test the array for duplicates at run start.

cmdlist = []
dupelist = []
dupecnt = 0
seqcnt = 0
for cmd in cmds:
  if cmd[Ddtmf] in cmdlist:
    dupecnt += 1
    dupelist.append(cmd[Ddtmf])
  else:
    cmdlist.append(cmd[Ddtmf])
  seqcnt += 1
  
del(cmdlist) # housekeeping: don't need this any more

print(f"{seqcnt} sequence{plurals(seqcnt)} {dupecnt} duplicate{plurals(dupecnt)}.")
if dupecnt > 0:
  print(f"Duplicate{plurals(dupecnt)}: {dupelist}")

del(dupelist) # housekeeping

# MQTT broker connection data
  
mqttc.will_set(baseTopic + "/LWT", payload="Online")
mqttc.reconnect_delay_set(min_delay=2, max_delay=64)
mqttc.username_pw_set(MQTTuser, MQTTpass)

print("Running...")
print("Ctrl-C to exit")

# Main loop

while (True):

  if not MQTTconnected:
    try:
      mqttc.connect(MQTTserver, 1883, 60) # THIS WILL FAIL if the script runs before the MQTT server starts (eg at a reboot)
    except:
      print("Connection fail.")      
    else:
      mqttc.loop_start() # non-blocking call to manage network keep-alive, receiver, etc.

  sleep(1)
