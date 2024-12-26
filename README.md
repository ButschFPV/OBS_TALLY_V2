# OBS_TALLY_V2

## EXPERIMENTAL obs-websocket 5.x.x support

Just another Arduino based Tally-light for OBS.
This Tally-light connects directly to the OBS WebSocket plugin via WiFi.

![IMG_live-active](/images/IMG_live-active.JPG)

### Features
- Simple adressable via a prefix in the OBS source name (use for example a prefix like: **ID1** or **CAM1**)
- Red LED for Live signal
- Green LED for preview signal (works only if you use OBS in Studio Mode)
- WiFi connectivity
- Uses only a few cheap components (no additional Raspberry Pi required)

### How does it work
The Tally-light connects to the OBS WebSocket plugin and subscribes to InputActiveStateChanged and InputShowStateChanged events.
Received events gets checked for the prefix in sourceName.

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
