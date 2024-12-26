/* 
   Fill in WIFI_SSID, WIFI_PASSWORD, WebSocket Host IP, WebSocket Port, WebSocket password (if required), OBS source name prefix (CAM1, CAM2, etc.) information

   On esp8266:
   Connect WS2812 LEDs to   D5 (GPIO14)
   Connect BUTTON to        D6 (GPIO12)
   Connect Potentiometer to A0

   OBS Websocket API protocol:  // https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md
*/

#include <ESP8266WiFi.h>
#include <ArduinoWebsockets.h>  // https://github.com/gilmaimon/ArduinoWebsockets
#include <ArduinoJson.h>        // https://arduinojson.org/v6/doc/
#include <Adafruit_NeoPixel.h>  // https://learn.adafruit.com/adafruit-neopixel-uberguide/arduino-library-use
#include <Crypto.h>
#include <SHA256.h>            // https://github.com/rweather/arduinolibs.git
#include "arduino_base64.hpp"  // https://github.com/dojyorin/arduino_base64

#define HASH_SIZE 32

#define UseWifiHeader false  // if you use a header file "myWiFi.h" for your WiFi credentials in the Arduino libraries folder, then set this to true
#if UseWifiHeader
#include "myWiFi.h"
#else
const char ssid[] = "WIFI_SSID";          // Your WiFi SSID
const char password[] = "WIFI_PASSWORD";  // Your WiFi Key
#endif

bool debug = false;

const char* websockets_server_host = "192.168.0.1";    // Enter server adress
const uint16_t websockets_server_port = 4455;          // Enter server port
const char websocket_password[] = "ABCDEFGH12345678";  // Enter websocket password if authentification is enabled
const char* sourceName_prefix = "ID1";                 // OBS source name prefix

// Pin configuration
#define LED_PIN D5     // GPIO14 connected to NeoPixels
#define BUTTON_PIN D6  // GPIO12 connected to Push-Button

// How many NeoPixels are attached?
#define LED_COUNT 6

// Which pin to use for ADC measurement
const int ADC_PIN = A0;  // ESP8266 Analog Pin ADC0 = A0

// Nothing to change from here on-------------------------------------------------------------------------------

// Interval time for different functions
const uint16_t PING_interval = 2500;  // Set Websocket ping interval to 2500ms
const uint8_t ADC_interval = 1;       // Set ADC poti update interval to 1ms

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
uint8_t LEDbrightness = 255;

// Define some nice colors
const uint32_t red = strip.Color(127, 0, 0);
const uint32_t green = strip.Color(0, 127, 0);
const uint32_t blue = strip.Color(0, 0, 127);
const uint32_t white = strip.Color(127, 127, 127);

// Interval time states
uint32_t ping_last = 0;  // stores the last time when ping was executed
uint32_t ADC_last = 0;   // stored the last time when the ADC took a measurement

// status flags
bool status_update = false;
bool brightness_update = false;
bool live_active = false;
bool preview_active = false;

// ADC configuration
const uint8_t ADCsamples = 20;  // get 10 samples and calculate the average
const uint8_t ADCdiff = 4;      // only update the brightness if the new value differs at least this much from the old value
uint8_t ADCpointer = 0;
uint16_t lastADCvalue = 0;
uint16_t currentADCvalue = 0;

using namespace websockets;

WebsocketsClient client;

JsonDocument doc;

SHA256 sha256;

// some functions

void ledMessage(uint16_t startLED, uint16_t countLED, uint32_t colorLED, uint16_t countFlashes, uint16_t delayFlashes);
void onMessageCallback(WebsocketsMessage message);
void onEventsCallback(WebsocketsEvent event, String data);
void ADC_handler(void);
void Websocket_handler(void);
void generateAuthString(const char* password, const char* salt, const char* challenge, char* authString);

void setup() {

  Serial.begin(115200);
  Serial.println();
  Serial.println("Serial initialized");

  Serial.println("Initialize the WS2812 LEDs");
  strip.begin();
  strip.show();  // Initialize all pixels to 'off'
  currentADCvalue = analogRead(ADC_PIN);
  LEDbrightness = map(currentADCvalue, 0, 1023, 40, 255);


  ledMessage(0, 1, white, 5, 100);
  Serial.println("WS2812 LEDs initialized");
  Serial.println();

  Serial.println("Initialize IOs");
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("IOs initialized");
  Serial.println();

  // Connect to wifi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  // Wait some time to connect to wifi
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    ledMessage(0, 1, blue, 1, 100);

    delay(500);
  }

  //WiFi.setAutoReconnect(true);

  delay(25);

  //Get Current Hostname
  Serial.print("Default hostname: ");
  Serial.println(WiFi.hostname());

  // set new hostname
  byte mac[6];
  WiFi.macAddress(mac);
  char newHostname[13];
  sprintf(newHostname, "TALLY-%02X%02X%02X\0", mac[3], mac[4], mac[5]);
  WiFi.hostname(newHostname);
  Serial.print("New hostname: ");
  Serial.println(newHostname);

  ledMessage(0, 1, green, 5, 50);

  Serial.println();

  Serial.print("Connected to Wifi: ");
  Serial.print(WiFi.SSID());
  Serial.println();

  // Setup Callbacks
  Serial.println("Initialize callbacks");
  client.onMessage(onMessageCallback);
  client.onEvent(onEventsCallback);
  Serial.println("Callbacks initialized");
  Serial.println();

  ledMessage(0, 1, green, 5, 50);

  Serial.println();

  //strip.clear();
  //strip.show();

  // try to connect to Websockets server
  Serial.println("Connect to Websocket server");
  bool connected = client.connect(websockets_server_host, websockets_server_port, "/");
  if (connected) {
    Serial.println("Connected!");
  } else {
    Serial.println("Not Connected! Try again later");
  }
  Serial.println();

  Serial.println("----setup-complete----");
  Serial.println("----start-loop----");
  Serial.println();
}

void loop() {

  // ----Websocket-handler----
  Websocket_handler();

  // ----status-update----
  if (status_update || brightness_update) {
    if (live_active) {
      if (!brightness_update) Serial.println("Turn on LIVE Signal");
      digitalWrite(LED_BUILTIN, LOW);
      strip.fill(strip.Color(LEDbrightness, 0, 0));
      strip.show();
    } else if (preview_active) {
      if (!brightness_update) Serial.println("Turn on preview Signal");
      digitalWrite(LED_BUILTIN, HIGH);
      strip.clear();
      strip.setPixelColor(0, strip.Color(0, LEDbrightness / 2, 0));
      strip.show();
    } else {
      if (!brightness_update) Serial.println("Turn off Signal");
      digitalWrite(LED_BUILTIN, HIGH);
      strip.clear();
      strip.show();
    }
    status_update = false;
    brightness_update = false;
  }

  // ----LED-Brightness----
  ADC_handler();

  yield();
}

// functions

void ledMessage(uint16_t startLED, uint16_t countLED, uint32_t colorLED, uint16_t countFlashes, uint16_t delayFlashes) {
  for (uint16_t i = 0; i < countFlashes; i++) {
    strip.fill(colorLED, startLED, countLED);
    strip.show();
    delay(delayFlashes / 2);
    strip.clear();
    strip.show();
    delay(delayFlashes / 2);
  }
}

void onMessageCallback(WebsocketsMessage message) {

  if (debug) {
    Serial.println("Got Message");
    Serial.print("Received Websocket Message Length: ");
    Serial.println(message.length());
    Serial.println();
    //Serial.println("Message content:");
    //Serial.println(message.data());
  }

  DeserializationError error = deserializeJson(doc, message.data());
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  } else {


    if (debug) {
      serializeJsonPretty(doc, Serial);
      Serial.println();
    }

    uint8_t opcode = doc["op"];  // convert json opcode to uint8_t

    switch (opcode) {
      // Hello (OpCode 0)
      case 0:
        {
          Serial.println("Hello (OpCode 0)");

          JsonDocument identify;

          identify["op"] = 1;
          JsonObject d = identify["d"].to<JsonObject>();
          //JsonObject d = identify.createNestedObject("d");
          d["rpcVersion"] = 1;

          const char* challenge = doc["d"]["authentication"]["challenge"];

          if (challenge) {
            Serial.println("Authentication is required");

            const char* salt = doc["d"]["authentication"]["salt"];

            // Authentifizierungsstring generieren
            char authString[45];
            generateAuthString(websocket_password, salt, challenge, authString);

            d["authentication"] = authString;

          } else {
            Serial.println("Authentication is not required");
          }

          //Serial.println("Füge den eventSubscription zum json hinzu");
          d["eventSubscriptions"] = (1 << 17) | (1 << 18);  // Subscribe to input events         (1<<17) = InputActiveStateChange    (1<<18) = InputShowStateChanged

          char output[192];
          serializeJson(identify, output);
          if (debug) {
            Serial.print("Send output: ");
            Serial.println(output);
          }

          client.send(output);
          break;
        }
      // Identified (OpCode 2)
      case 2:
        {
          Serial.println("Identified (OpCode 2)");
          Serial.println("Create Request (OpCode 6)");

          // ----GetInputList-request----
          JsonDocument request;

          request["op"] = 6;

          JsonObject d = request["d"].to<JsonObject>();
          d["requestType"] = "GetInputList";
          d["requestId"] = "f829dcf0-89cc-11eb-8f0e-382c4ac93b9c";

          char output[256];
          serializeJson(request, output);
          if (debug) {
            Serial.print("Send output: ");
            Serial.println(output);
          }

          client.send(output);

          break;
        }

      // Event (OpCode 5)
      case 5:
        {
          Serial.println("Event (OpCode 5)");

          const char* inputName = doc["d"]["eventData"]["inputName"];

          if (strncmp(sourceName_prefix, inputName, strlen(sourceName_prefix)) == 0) {
            Serial.println("prefix match");
            const uint32_t eventIntent = doc["d"]["eventIntent"];

            if (eventIntent == (1 << 17))  // InputActiveStateChanged  - Program state change
            {
              live_active = doc["d"]["eventData"]["videoActive"];
            } else if (eventIntent == (1 << 18))  // InputShowStateChanged  - Preview state change
            {
              preview_active = doc["d"]["eventData"]["videoShowing"];
            } else {
              Serial.println("unknown eventIntent");
            }

            status_update = true;

          } else {
            Serial.println("prefix does not match");
          }

          break;
        }

      // RequestResponse (OpCode 7)
      case 7:
        {
          Serial.println("RequestResponse (OpCode 7)");

          char requestId_GetSourceActive[] = "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c";
          char requestId_GetInputList[] = "f829dcf0-89cc-11eb-8f0e-382c4ac93b9c";

          if (doc["d"]["requestStatus"]["result"]) {
            if (strncmp(doc["d"]["requestId"], requestId_GetSourceActive, strlen(requestId_GetSourceActive)) == 0) {
              if (doc["d"]["requestStatus"]["result"]) {
                live_active = doc["d"]["responseData"]["videoActive"];
                preview_active = doc["d"]["responseData"]["videoShowing"];
                status_update = true;
              }
            } else if (strncmp(doc["d"]["requestId"], requestId_GetInputList, strlen(requestId_GetInputList)) == 0) {

              for (uint8_t i = 0; i < doc["d"]["responseData"]["inputs"].size(); i++) {
                if (strncmp(sourceName_prefix, doc["d"]["responseData"]["inputs"][i]["inputName"], strlen(sourceName_prefix)) == 0) {
                  const char* match = doc["d"]["responseData"]["inputs"][i]["inputName"];
                  Serial.print("Match found: ");
                  Serial.println(match);

                  // ----request----
                  JsonDocument request;

                  request["op"] = 6;

                  JsonObject d = request["d"].to<JsonObject>();
                  d["requestType"] = "GetSourceActive";
                  d["requestId"] = "f819dcf0-89cc-11eb-8f0e-382c4ac93b9c";
                  d["requestData"]["sourceName"] = match;

                  char output[256];
                  serializeJson(request, output);
                  if (debug) {
                    Serial.print("Send output: ");
                    Serial.println(output);
                  }

                  client.send(output);

                  break;
                }
              }
            }
          }

          break;
        }

      default:
        {
          Serial.println("op code not supported");
          break;
        }
    }
  }
}

void onEventsCallback(WebsocketsEvent event, String data) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("Connnection Opened");
    strip.clear();
    //ledMessage(0, 1, green, 5, 100);
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("Connnection Closed");
    live_active = false;
    preview_active = false;
    status_update = true;
  } else if (event == WebsocketsEvent::GotPing) {
    if (debug) Serial.println("Got a Ping!");
  } else if (event == WebsocketsEvent::GotPong) {
    if (debug) Serial.println("Got a Pong!");
  }
}

void ADC_handler(void) {
  uint32_t temp = millis();
  if (temp > (ADC_last + ADC_interval)) {
    if (ADCpointer < ADCsamples) {
      currentADCvalue = (analogRead(ADC_PIN) + currentADCvalue) / 2;
      ADCpointer++;
    } else {
      ADCpointer = 0;

      if ((currentADCvalue >= (lastADCvalue + ADCdiff)) || (currentADCvalue <= (lastADCvalue - ADCdiff))) {
        LEDbrightness = map(currentADCvalue, 0, 1023, 40, 255);
        brightness_update = true;
        lastADCvalue = currentADCvalue;
      }
    }
    ADC_last = temp;
  }
}

void Websocket_handler(void) {
  if (client.available()) {
    client.poll();  // let the websockets client check for incoming messages

    //ping host
    if ((millis() - ping_last) >= PING_interval) {
      ping_last = millis();
      client.ping();
    }

  } else {
    Serial.print("Connecting to server: ");
    // try to connect to Websockets server
    bool connected = client.connect(websockets_server_host, websockets_server_port, "/");
    if (connected) {
      Serial.println("Connecetd!");
    } else {
      Serial.println("Not Connected!");
    }
    delay(1000);
  }
}


void generateAuthString(const char* password, const char* salt, const char* challenge, char* authString) {
  // Passwort und Salt konkatenieren
  char passwordAndSalt[sizeof(password) + 45];
  strcpy(passwordAndSalt, password);
  strcat(passwordAndSalt, salt);

  // SHA256-Hash des Passworts und Salts berechnen
  uint8_t hash[HASH_SIZE];
  sha256.reset();
  sha256.update(passwordAndSalt, strlen(passwordAndSalt));
  sha256.finalize(hash, HASH_SIZE);

  // Base64-Kodierung des Hashes
  char base64Secret[45];
  BASE64::encode(hash, HASH_SIZE, base64Secret);

  // Base64-Secret und Challenge konkatenieren
  char secretAndChallenge[sizeof(base64Secret) + 45];
  strcpy(secretAndChallenge, base64Secret);
  strcat(secretAndChallenge, challenge);

  // SHA256-Hash des kombinierten Strings berechnen
  uint8_t combinedHash[HASH_SIZE];
  sha256.reset();
  sha256.update(secretAndChallenge, strlen(secretAndChallenge));
  sha256.finalize(combinedHash, HASH_SIZE);

  // Base64-Kodierung des kombinierten Hashes
  BASE64::encode(combinedHash, HASH_SIZE, authString);
}
