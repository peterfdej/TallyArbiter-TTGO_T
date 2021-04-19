Tally Arbiter TTGO_T Listener is a rewrite of Tally Arbiter M5Stick-C Listener, written by Joseph Adams, for a TTGO_T-Display device. (https://github.com/josephdadams/TallyArbiter-M5StickCListener)

I have added a 3 color LED (connected with 3x 270 Ohm resistors) and an on/off switch for the battery.
The backlight can be switch on or off with the lower button.
The battery is an 800mAh lithium cell.
When Voltage drops under 2.8V, it goes into sleep mode. Restart with top button.
Hold top button when startup, put it in setting mode. 
In setting mode it acts as an AP. Browse to 192.168.4.1 and you can set ssid, password and tallyarbiter server adres with port.

For the case I used the front panel of pjmi on Thingiverse: https://www.thingiverse.com/thing:4501444
The back case is redesigned for holding a 10mm 3 color led, a battery and a camera shoe mount.

I installed TallyArbiter on a pi. For the correct versions of node I used https://github.com/audstanley/NodeJs-Raspberry-Pi.
For starting TallyArbiter at boot, I put these 4 lines in /etc/rc.local.
(
cd /home/pi/tallyarbiter
node /home/pi/tallyarbiter/index.js &
)
pm2 didn't work for me.