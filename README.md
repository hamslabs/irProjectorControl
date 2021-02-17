# irProjectorControl
Control a Viewsonic M1 Mini by sending IR Remote Commands using ESP32. This is using a ESP32-S2 Board from Adafruit
called FeatherS2 with a LIPO battery. It detects USB power off and then turns the projector off.

Send NEC remote control code with an IR LED to do 2 things.

- Turn the projector on and select the first media file to play (pwr-right-right-OK-down-OK)
- Turn the projector off (pwr-pwr) and go to low power mode.
