/* Do not activate OBS WebSocket authentication!

   Fill in SSID, password, WebSocket Host, prefix information

   Connect WS2812b DIN LEDs to D5 (GPIO14)
   Connect BUTTON to           D6 (GPIO12)
   Connect Potentiometer to    A0

   OBS Websocket API protocol:      // https://github.com/Palakis/obs-websocket/blob/4.x-current/docs/generated/protocol.md
*/

#include <ESP8266WiFi.h>
#include <ArduinoWebsockets.h>      // https://github.com/gilmaimon/ArduinoWebsockets
#include <ArduinoJson.h>            // https://arduinojson.org/v6/doc/
#include <Adafruit_NeoPixel.h>      // https://learn.adafruit.com/adafruit-neopixel-uberguide/arduino-library-use

#define DEBUG_ON    (bool)false     // activate or deactivate the debug information

#define UseWifiHeader false          // if you use a header file "myWiFi.h" for your WiFi credentials in the Arduino libraries folder, then set this to true
#if UseWifiHeader
#include "../myWiFi.h"
#else
const char ssid[] = "SSID";        // Your WiFi SSID
const char pass[] = "PASSWORD";   // Your WiFi Key
#endif

const char* websockets_server_host = "192.168.13.37";   // Enter Websocket server IP adress
const uint16_t websockets_server_port = 4444;           // Enter Websocket server port
const char* prefix = "ID1";                             // OBS source name prefix

// Pin configuration
#define LED_PIN    D5      // GPIO14 connected to NeoPixels
#define BUTTON_PIN  D6     // GPIO12 connected to Push-Button

// How many NeoPixels are attached?
#define LED_COUNT  6

// Which pin to use for ADC measurement
const int ADC_PIN = A0;  // ESP8266 Analog Pin ADC0 = A0

// Nothing to change from here on-------------------------------

// Interval time for different functions
const uint16_t DEBUG_interval = 2000;   // Set DEBUG_ON report interval to 2000ms
const uint16_t PING_interval = 2500;    // Set Websocket ping interval to 2500ms
const uint8_t ADC_interval = 1;         // Set ADC poti update interval to 1ms

// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
uint8_t LEDbrightness = 255;

// Define some nice colors
const uint32_t red = strip.Color(127, 0, 0);
const uint32_t green = strip.Color(0, 127, 0);
const uint32_t blue = strip.Color(0, 0, 127);
const uint32_t white = strip.Color(127, 127, 127);

// Interval time states
uint32_t DEBUG_last = 0;    // stores the last time when the DEBUG_ON report was printed
uint32_t ping_last = 0;     // stores the last time when ping was executed
uint32_t ADC_last = 0;      // stored the last time when the ADC took a measurement

//
bool status_update = false;
bool live_active = false;
bool preview_active = false;

// ADC configuration
const uint8_t ADCsamples = 20;     // get 10 samples and calculate the average
const uint8_t ADCdiff = 6;         // only update the brightness if the new value differs at least this much from the old value
uint8_t ADCpointer = 0;
uint16_t lastADCvalue = 0;
uint16_t currentADCvalue = 0;

using namespace websockets;

WebsocketsClient client;

StaticJsonDocument<2048> doc;

StaticJsonDocument<256> filter;

// some functions

void ledMessage(uint16_t startLED, uint16_t countLED, uint32_t colorLED, uint16_t countFlashes, uint16_t delayFlashes);
void onMessageCallback(WebsocketsMessage message);
void onEventsCallback(WebsocketsEvent event, String data);
void ADC_handler(void);
void Websocket_handler(void);

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("Serial initialized");

  Serial.println("Initialize the WS2812 LEDs");
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
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

  //JSON filter configuration
  Serial.println("Initialize JSON filter");
  filter["sources"][0]["name"] = true;
  filter["update-type"] = true;
  Serial.println("JSON filter initialized");
  Serial.println();

  // Setup Callbacks
  Serial.println("Initialize callbacks");
  client.onMessage(onMessageCallback);
  client.onEvent(onEventsCallback);
  Serial.println("Callbacks initialized");
  Serial.println();

  // Connect to wifi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, pass);

  // Wait some time to connect to wifi
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    ledMessage(0, 1, blue, 1, 100);

    delay(500);
  }

  ledMessage(0, 1, green, 5, 50);

  Serial.println();

  Serial.println("Connected to Wifi");
  Serial.println();

  // try to connect to Websockets server
  Serial.println("Connect to Websocket server");
  bool connected = client.connect(websockets_server_host, websockets_server_port, "/");
  if (connected)
  {
    Serial.println("Connected!");
  }
  else
  {
    Serial.println("Not Connected! Try again later");
  }
  Serial.println();

  Serial.println("----setup-complete----");
  Serial.println("----start-loop----");
  Serial.println();

}

void loop()
{

  // ----Websocket-handler----
  Websocket_handler();


  if (status_update)
  {
    if (live_active)
    {
      if (DEBUG_ON)
      {
        Serial.println("Turn on LIVE Signal");
      }

      digitalWrite(LED_BUILTIN, LOW);
      strip.fill(strip.Color(LEDbrightness, 0, 0));
      strip.show();
    }
    else if (preview_active)
    {
      if (DEBUG_ON)
      {
        Serial.println("Turn on preview Signal");
      }

      digitalWrite(LED_BUILTIN, HIGH);
      strip.clear();
      strip.setPixelColor(0, strip.Color(0, LEDbrightness / 2, 0));
      strip.show();
    }
    else if ((!live_active) && (!preview_active))
    {
      if (DEBUG_ON)
      {
        Serial.println("Turn off Signal");
      }

      digitalWrite(LED_BUILTIN, HIGH);
      strip.clear();
      strip.show();
    }
    status_update = false;
  }

  // ----LED-Brightness----
  ADC_handler();

  // ----DEBUG-report----
  if (DEBUG_ON)
  {
    uint32_t DEBUG_temp = millis();
    if (DEBUG_temp > (DEBUG_last + DEBUG_interval))
    {
      Serial.println();
      Serial.println("DEBUG report------------------");
      Serial.print("Button status: ");
      if (digitalRead(BUTTON_PIN) == HIGH)
      {
        Serial.println("not pushed");
      }
      else
      {
        Serial.println("pushed");
      }
      Serial.print("ADC value: ");
      Serial.println(currentADCvalue);
      Serial.print("LED Brightness value: ");
      Serial.println(LEDbrightness);
      Serial.println("DEBUG report end--------------");
      Serial.println();
      DEBUG_last = DEBUG_temp;
    }
  }

  yield();

}

// functions

void ledMessage(uint16_t startLED, uint16_t countLED, uint32_t colorLED, uint16_t countFlashes, uint16_t delayFlashes)
{
  for (uint16_t i = 0; i < countFlashes; i++)
  {
    strip.fill(colorLED, startLED, countLED);
    strip.show();
    delay(delayFlashes / 2);
    strip.clear();
    strip.show();
    delay(delayFlashes / 2);
  }
}

void onMessageCallback(WebsocketsMessage message)
{
  if (DEBUG_ON)
  {
    Serial.println("Got Message");
    Serial.print("Websocket Message Length: ");
    Serial.println(message.length());
    Serial.println();
    //Serial.println("Message content:");
    //Serial.println(message.data());       // Prints the RAW Websocket message, this can take some time be careful
  }



  DeserializationError error = deserializeJson(doc, message.data(), DeserializationOption::Filter(filter));
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  else
  {

    if (DEBUG_ON)
    {
		Serial.print("JSON filtered message size: ");
		Serial.println(doc.memoryUsage());
      //serializeJsonPretty(doc, Serial);
    }

    if (strcmp(doc["update-type"], "SwitchScenes") == 0)
    {
      bool live = false;

      for (uint8_t i = 0; i < doc["sources"].size(); i++)
      {
        if (strncmp(prefix, doc["sources"][i]["name"], strlen(prefix)) == 0)
        {
          if (DEBUG_ON)
          {
            Serial.print("\"");
            Serial.print(prefix);
            Serial.println("\" live!!!");
          }

          live = true;
          break;

        }
      }

      if (live)
      {
        if (!live_active)
        {
          if (DEBUG_ON)
          {
            Serial.println("live active");
          }

          live_active = true;
          status_update = true;
        }

      }
      else
      {
        if (live_active)
        {
          if (DEBUG_ON)
          {
            Serial.println("live not active");
          }

          live_active = false;
          status_update = true;
        }
      }
    }
    else if (strcmp(doc["update-type"], "PreviewSceneChanged") == 0)
    {
      bool preview = false;

      for (uint8_t i = 0; i < doc["sources"].size(); i++)
      {
        if (strncmp(prefix, doc["sources"][i]["name"], strlen(prefix)) == 0)  // contains the source name the prefix
        {
          if (DEBUG_ON)
          {
            Serial.print("\"");
            Serial.print(prefix);
            Serial.println("\" preview");
          }
          preview = true;
          break;
        }
      }

      if (preview)
      {
        if (!preview_active)
        {
          preview_active = true;
          status_update = true;
          if (DEBUG_ON)
          {
            Serial.println("Preview active");
          }

        }
      }
      else
      {
        if (preview_active)
        {
          preview_active = false;
          status_update = true;
          if (DEBUG_ON)
          {
            Serial.println("Preview not active");
          }

        }
      }
    }

  }

}

void onEventsCallback(WebsocketsEvent event, String data)
{
  if (event == WebsocketsEvent::ConnectionOpened)
  {
    Serial.println("Connnection Opened");

  }
  else if (event == WebsocketsEvent::ConnectionClosed)
  {
    Serial.println("Connnection Closed");
    live_active = false;
    preview_active = false;

    status_update = true;
  }
  else if (event == WebsocketsEvent::GotPing)
  {
    if (DEBUG_ON)
    {
      Serial.println("Got a Ping!");
    }

  }
  else if (event == WebsocketsEvent::GotPong)
  {
    if (DEBUG_ON)
    {
      Serial.println("Got a Pong!");
    }

  }
}

void ADC_handler(void)
{
  uint32_t temp = millis();
  if (temp > (ADC_last + ADC_interval))
  {
    if (ADCpointer < ADCsamples)
    {
      currentADCvalue = (analogRead(ADC_PIN) + currentADCvalue) / 2;
      ADCpointer++;
    }
    else
    {
      ADCpointer = 0;

      if ((currentADCvalue >= (lastADCvalue + ADCdiff)) || (currentADCvalue <= (lastADCvalue - ADCdiff)))
      {
        LEDbrightness = map(currentADCvalue, 0, 1023, 40, 255);
        status_update = true;
        lastADCvalue = currentADCvalue;
      }
    }
    ADC_last = temp;
  }
}

void Websocket_handler(void)
{
  if (client.available())
  {
    client.poll();  // let the websockets client check for incoming messages

    //ping host
    if ((millis() - ping_last) >= PING_interval)
    {
      ping_last = millis();
      client.ping();
    }

  }
  else
  {
    Serial.print("Connecting to server: ");
    // try to connect to Websockets server
    bool connected = client.connect(websockets_server_host, websockets_server_port, "/");
    if (connected)
    {
      Serial.println("Connecetd!");
    }
    else
    {
      Serial.println("Not Connected!");
    }
    delay(1000);
  }
}
