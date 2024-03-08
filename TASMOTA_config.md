# HaMQTT configuration for TASMOTA
_Pronouced Ham Cue Tee Tee_

**An MQTT Interface to an Amateur Radio Station**

The following configuration should be applied to the TASMOTA device. You must substiture the IP address of your own MQTT broker, along with the desired uername and password.

```
backlog hostname iotHaMQTT1; topic hamqtt; friendlyname HaMQTT; devicename HaMQTT
backlog mqtthost 192.168.1.12; mqttuser luser; mqttpassword pass
backlog TimeZone 99; TimeDST 0,0,3,1,1,60; TimeSTD 0,0,10,1,2,0
backlog setoption19 0; setoption56 1; setoption53 1; fulltopic %topic%/%prefix%/
backlog latitude 52.3; longitude -2
```

This is specific to the D1 Mini. Since we know we’ll be using the hardware serial port, the IO pin configuration can be set as follows: 
```
backlog template {"NAME":"WemosD1Mini","GPIO":[1,3200,1,3232,1,1,0,0,1,1,1,1,0,4704],"FLAG":0,"BASE":18}; module 0
```
The Arduino program is configured for 9600 baud, so we must:
```
baudrate 9600
```
The defaults (8,N,1) for start/parity/stop bits are fine. The default SerialDelimiter setting (255=none) seems OKay as well. I don’t understand how it asserts that a complete message has been received.

Serial receive will not happen until TASMOTA is first instructed to perform a serialsend. This is believed to be a security measure. A rule has been created to ensure this happens at boot:
```
rule1 on system#boot do serialsend START endon; rule1 1
```
