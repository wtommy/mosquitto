This describes how to run Mosquitto as a Windows service.

* First, download the Mosquitto Windows binary and unzip to the location you wish to install it from.

* Now download Windows Service Wrapper (winsw) from:

http://maven.dyndns.org/2/com/sun/winsw/winsw/

The latest version as of this writing is winsw-1.8-bin.exe.

* Place winsw in the mosquitto directory, and rename it to mosquitto-svc.exe.

* Copy mosquitto-svc.xml from this directory to the mosquitto directory.

* Open a command prompt and change directory to the mosquitto directory.

* Run "mosquitto-svc install".

You should now see a Mosquitto entry in the Windows Services control panel and will be able to start/stop it. To uninstall, run "mosquitto-svc uninstall".
