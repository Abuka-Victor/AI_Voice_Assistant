#define VERSION "\n=== KALO ESP32 Voice Assistant (last update: July 22, 2024) ======================"

#include <WiFi.h>  // only included here
#include <SD.h>    // also needed in other tabs (.ino)

#include <Audio.h>  // needed for PLAYING Audio (via I2S Amplifier, e.g. MAX98357) with ..
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SimpleTimer.h>

String text;
String filteredAnswer = "";
String repeat;
SimpleTimer Timer;
float batteryVoltage;

const char* ssid = "NITDA-ICT-HUB";                                                       // ## INSERT your wlan ssid
const char* password = "6666.2524#";                                                // ## INSERT your password
const char* OPENAI_KEY = "***************************************";  // ## optionally (needed for Open AI voices): INSERT your OpenAI key
const char* gemini_KEY = "AIzaSyAioyaH2qul23iCIOrdyW8un_Cc-9MR_Ek";                   //gemini api
#define TTS_MODEL 0                                                               // 1 = OpenAI TTS; 0 = Google TTS

String OpenAI_Model = "gpt-3.5-turbo-instruct";  // Model
String OpenAI_Temperature = "0.20";              // temperature
String OpenAI_Max_Tokens = "100";                //Max Tokens

#define AUDIO_FILE "/Audio.wav"  // mandatory, filename for the AUDIO recording

#define TTS_GOOGLE_LANGUAGE "en-US"  // needed for Google TTS voices only (not needed for multilingual OpenAI voices :) \

#define pin_RECORD_BTN 16
#define pin_VOL_POTI 15
#define pin_repeat 22

#define pin_LED_RED 17
#define pin_LED_GREEN 12
#define pin_LED_BLUE 13

#define pin_I2S_DOUT 34  // 3 pins for I2S Audio Output (Schreibfaul1 audio.h Library)
#define pin_I2S_LRC 26
#define pin_I2S_BCLK 35

Audio audio_play;
bool I2S_Record_Init();
bool Record_Start(String filename);
bool Record_Available(String filename, float* audiolength_sec);

String SpeechToText_Deepgram(String filename);
void Deepgram_KeepAlive();
const int batteryPin = 34;             // Pin 34 for battery voltage reading
const float R1 = 100000.0;             // 100k ohm resistor
const float R2 = 10000.0;              // 10k ohm resistor
const float adcMax = 4095.0;           // Max value for ADC on ESP32
const float vRef = 3.4;                // Reference voltage for ESP32
const int numSamples = 100;            // Number of samples for averaging
const float calibrationFactor = 1.48;  // Calibration factor for ADC reading

// ------------------------------------------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);  // 10 times faster reaction after CR entered (default is 1000ms)
  pinMode(batteryPin, INPUT);
  analogReadResolution(12);  // 12-bit ADC resolution

  pinMode(pin_LED_RED, OUTPUT);
  pinMode(pin_LED_GREEN, OUTPUT);
  pinMode(pin_LED_BLUE, OUTPUT);
  pinMode(pin_RECORD_BTN, INPUT_PULLUP);  // use INPUT_PULLUP if no external Pull-Up connected ##
  pinMode(pin_repeat, INPUT_PULLUP);
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);

  led_RGB(50, 0, 0);
  delay(500);
  led_RGB(0, 50, 0);
  delay(500);
  led_RGB(0, 0, 50);
  delay(500);
  led_RGB(0, 0, 0);

  // Serial.println(VERSION);
  Timer.setInterval(10000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WLAN ");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(". Done, device connected.");
  led_RGB(0, 50, 0);  // GREEN

  if (!SD.begin()) {
    Serial.println("ERROR - SD Card initialization failed!");
    return;
  }

  I2S_Record_Init();

  audio_play.setPinout(pin_I2S_BCLK, pin_I2S_LRC, pin_I2S_DOUT);

  audio_play.setVolume(21);  //21
  Serial.println("> HOLD button for recording AUDIO .. RELEASE button for REPLAY & Deepgram transcription");
}

void loop() {
here:

  if (digitalRead(pin_RECORD_BTN) == LOW)  // Recording started (ongoing)
  {
    led_RGB(50, 0, 0);  //  RED means 'Recording ongoing'
    delay(30);          // unbouncing & suppressing button 'click' noise in begin of audio recording

    // Before we start any recording we stop any earlier Audio Output or streaming (e.g. radio)
    if (audio_play.isRunning()) {
      audio_play.connecttohost("");  // 'audio_play.stopSong()' wouldn't be enough (STT wouldn't reconnect)
    }

    //Start Recording
    Record_Start(AUDIO_FILE);
  }

  if (digitalRead(pin_RECORD_BTN) == HIGH)  // Recording not started yet .. OR stopped now (on release button)
  {
    led_RGB(0, 0, 0);

    float recorded_seconds;
    if (Record_Available(AUDIO_FILE, &recorded_seconds))  //  true once when recording finalized (.wav file available)
    {
      if (recorded_seconds > 0.4)  // ignore short btn TOUCH (e.g. <0.4 secs, used for 'audio_play.stopSong' only)
      {
        String transcription = SpeechToText_Deepgram(AUDIO_FILE);
        String again = "Please Ask Again . . . . . . . . . . . ";


        Serial.println(transcription);
        if (transcription == "") {
          led_RGB(0, 0, 255);
          speakTextInChunks(again, 93);  // ( Uncomment this to use Google TTS )
          Serial.println("Please Ask Again");
          while (audio_play.isRunning())  // wait here until finished (just for Demo purposes, before we play Demo 4)
          {
            audio_play.loop();
          }
          goto here;
        }

        WiFiClientSecure client;
        client.setInsecure();  // Disable SSL verification for simplicity (not recommended for production)
        String Answer = "";    // Declare Answer variable here

        text = "";

        if (client.connect("generativelanguage.googleapis.com", 443)) {
          String url = "/v1beta/models/gemini-1.5-flash:generateContent?key=" + String(gemini_KEY);

          String payload = String("{\"contents\": [{\"parts\":[{\"text\":\"" + transcription + "\"}]}],\"generationConfig\": {\"maxOutputTokens\": " + OpenAI_Max_Tokens + "}}");

          client.println("POST " + url + " HTTP/1.1");
          client.println("Host: generativelanguage.googleapis.com");
          client.println("Content-Type: application/json");
          client.print("Content-Length: ");
          client.println(payload.length());
          client.println();
          client.println(payload);

          String response;
          while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
              break;
            }
          }

          response = client.readString();
          parseResponse(response);
        } else {
          Serial.println("Connection failed!");
        }

        client.stop();  // End the connection

        if (filteredAnswer != "")  // we found spoken text .. now starting Demo examples:
        {
          led_RGB(0, 0, 255);
          Serial.print("OpenAI speaking: ");
          Serial.println(filteredAnswer);
          speakTextInChunks(filteredAnswer, 93);  // ( Uncomment this to use Google TTS )
          
        }
      }
    }
  }

  if (digitalRead(pin_repeat) == LOW) {
    delay(500);
    analogWrite(pin_LED_BLUE, 255);
    Serial.print("repeat - ");
    Serial.println(repeat);
    speakTextInChunks(repeat, 93);  // ( Uncomment this to use Google TTS )
  }

  audio_play.loop();

  if (audio_play.isRunning()) {

    analogWrite(pin_LED_BLUE, 255);
    if (digitalRead(pin_RECORD_BTN) == LOW) {
      goto here;
    }
  } else {


    analogWrite(pin_LED_BLUE, 0);
  }

  String batt = "battery low. please charge";
  if (Timer.isReady()) {
    Serial.print("Battery Voltage: ");
    Serial.println(batteryVoltage);
    if (batteryVoltage < 3.4) {
      speakTextInChunks(batt.c_str(), 93);  // ( Uncomment this to use Google TTS )
    }

    Timer.reset();
  }

  if (digitalRead(pin_RECORD_BTN) == HIGH && !audio_play.isRunning())  // but don't do it during recording or playing
  {
    static uint32_t millis_ping_before;
    if (millis() > (millis_ping_before + 5000)) {
      millis_ping_before = millis();
      led_RGB(0, 0, 0);  // short LED OFF means: 'Reconnection server, can't record in moment'
      Deepgram_KeepAlive();
    }
  }
}

void speakTextInChunks(String text, int maxLength) {
  int start = 0;
  while (start < text.length()) {
    int end = start + maxLength;

    // Ensure we don't split in the middle of a word
    if (end < text.length()) {
      while (end > start && text[end] != ' ' && text[end] != '.' && text[end] != ',') {
        end--;
      }
    }

    // If no space or punctuation is found, just split at maxLength
    if (end == start) {
      end = start + maxLength;
    }

    String chunk = text.substring(start, end);
    audio_play.connecttospeech(chunk.c_str(), TTS_GOOGLE_LANGUAGE);

    while (audio_play.isRunning()) {
      audio_play.loop();
      if (digitalRead(pin_RECORD_BTN) == LOW) {
        break;
      }
    }

    start = end + 1;  // Move to the next part, skipping the space
                      // delay(200);       // Small delay between chunks
  }
}

void parseResponse(String response) {
  repeat = "";
  // Extract JSON part from the response
  int jsonStartIndex = response.indexOf("{");
  int jsonEndIndex = response.lastIndexOf("}");

  if (jsonStartIndex != -1 && jsonEndIndex != -1) {
    String jsonPart = response.substring(jsonStartIndex, jsonEndIndex + 1);

    DynamicJsonDocument doc(1024);  // Increase size if needed
    DeserializationError error = deserializeJson(doc, jsonPart);

    if (error) {
      Serial.print("DeserializeJson failed: ");
      Serial.println(error.c_str());
      return;
    }

    if (doc.containsKey("candidates")) {
      for (const auto& candidate : doc["candidates"].as<JsonArray>()) {
        if (candidate.containsKey("content") && candidate["content"].containsKey("parts")) {

          for (const auto& part : candidate["content"]["parts"].as<JsonArray>()) {
            if (part.containsKey("text")) {
              text += part["text"].as<String>();
            }
          }
          text.trim();

          filteredAnswer = "";
          for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            if (isalnum(c) || isspace(c) || c == ',' || c == '.' || c == '\'') {
              filteredAnswer += c;
            } else {
              filteredAnswer += ' ';
            }
          }

          repeat = filteredAnswer;
        }
      }
    } else {
      Serial.println("No 'candidates' field found in JSON response.");
    }
  } else {
    Serial.println("No valid JSON found in the response.");
  }
}


void led_RGB(int red, int green, int blue) {
  static bool red_before, green_before, blue_before;
  if (red != red_before) {
    analogWrite(pin_LED_RED, red);
    red_before = red;
  }
  if (green != green_before) {
    analogWrite(pin_LED_GREEN, green);
    green_before = green;
  }
  if (blue != blue_before) {
    analogWrite(pin_LED_BLUE, blue);
    blue_before = blue;
  }
}
