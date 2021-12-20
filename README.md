# OBS_TALLY_V2

## WARNING the code does not work reliable anymore.

Just another Arduino based Tally-light for OBS.
This Tally-light connects directly to the OBS WebSocket plugin via WiFi.

![IMG_live-active](/images/IMG_live-active.JPG)

### Features
- Simple adressable via a prefix in the OBS source name (use for example a prefix like: **ID1** or **CAM1**)
- Green LED for preview signal (works only if you use OBS in Studio Mode)
- WiFi connectivity
- Uses only a few cheap components (no additional Raspberry Pi required)

### How does it work
The Tally-light connects to OBS WebSocket plugin and gets updates when for example the scene changes.
Those messages contains all used sources from the active scene in a JSON format.
The Controller filters the JSON and looks for the programmed prefix in the source name.

### Images
![IMG_preview-active](/images/IMG_preview-active.JPG)
![IMG_without-cover](/images/IMG_without-cover.JPG)
![IMG_without-diffusor](/images/IMG_without-diffusor.JPG)

### What you need
- 3D Printer
- Diffusor cover from an defect E27 LED Light Bulb
- LoLin NodeMcu V3 board (NodeMCU clone)
- WS2812b LED strip 60LEDs/m
- Potentiometer
- 4x M3x5mm cylinder head screw
- 5x 3x16mm flat head screw

### Limits
The WebSocket messages can get very big, this is a problem for the small microcontroller.
I recommend to not have more than 10 sources in a scene.
If you enable debugging you can see the message and JSON Size in the serial monitor.
