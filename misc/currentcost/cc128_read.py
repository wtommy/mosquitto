#!/usr/bin/python -u

import serial
import shlex
import subprocess

pub_cmd = "mosquitto_pub -t cc128/raw -l -q 2"
pub_args = shlex.split(pub_cmd)
pub = subprocess.Popen(pub_args, stdin=subprocess.PIPE)

usb = serial.Serial(port='/dev/ttyUSB0', baudrate=57600)


running = True
try:
	while running:
		line = usb.readline()
		pub.stdin.write(line)
		pub.stdin.flush()
except usb.SerialException, e:
	running = False

pub.stdin.close()

pub.wait()

