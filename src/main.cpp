#include <Arduino.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip.
#include <SPI.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
//#include <ESPmDNS.h>
#include "pitches.h"
#include "Music.h"
#include <credentials.h>

//#include "NotoSansBold15.h"
//#include "NotoSansBold36.h"

//#define FONT_SMALL NotoSansBold15                            // Fonts
//#define FONT_LARGE NotoSansBold36
#define BgColour TFT_BLACK
#define FgColour TFT_WHITE

#define AutoEnter 1.5                                        // Auto accept cooking time
#define Buffer 1.25                                          // Delay magnetron
#define Debug 0                                              // 0-off 1-on
#define scrRefresh 10                                       // seconds

// Define pins
#define ENC_A 32
#define ENC_B 33
#define SWTCH 21
#define Door 26
#define Light 14
#define Mag 27
#define Spkr 25

const char *ssid     = SSID;
const char *password = PASS;
const String endpoint = OpenWeatherCall;
const String key = OpenWeatherKey;

String quoteLN1;
String quoteLN2;
String quoteLN3;
String quoteLN4;
String quoteLN5;

struct tm  ts;
char buf[80];
String temperature;
String description;
String data;

unsigned long refreshTime = 0;
unsigned long refreshWeather = 0;

String payload = ""; //weather

unsigned long timer = 0;           // Cooking time
unsigned long timeout = 0;         // Menu Timer
unsigned long paused = 0;          // Cooking time at Door Open
unsigned long magwait = 1;         // Delay Mag
unsigned long refreshTimer = 0;    // Screen refresh timer
unsigned long debugtimer = 1;      // Debug timer
unsigned long selectedSeconds = 0;   // Time selected at last encoder event
unsigned long wifiQuietUntil = 0;    // Pause WiFi after encoder changes
bool door;                         // Door Status
bool ScrRefresh = 1;               // Screen rfsh Status

unsigned int mode = 0;             //Program mode

int counter = 0; // Absolute Encoder position
int encoder = 1; // Relative Encoder position

uint8_t hh=00, mm=00, ss=00;  //  H, M, S Declaration

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

NonBlockingMelody buzzer(Spkr);

// Connects to WiFi STA with a static IP config.
void connectWiFi() {
  IPAddress local_IP(192, 168, 15, 151);
  IPAddress gateway(192, 168, 15, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns1(192, 168, 15, 1);
  IPAddress dns2(0, 0, 0, 0);
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet, dns1, dns2);
  WiFi.setHostname("MicroOndas");
  WiFi.begin(ssid, password);
}

// Pauses the WiFi radio and turns it completely OFF.
// This is used during encoder rotation and active cooking to prevent 
// radio frequency noise and connection lags from blocking the loop.
void pauseWiFiFor(unsigned long durationMs) {
  wifiQuietUntil = millis() + durationMs;
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

// Manages the state of the WiFi radio based on current microwave Mode.
// - CLOCK (Mode 1): Turns WiFi ON and reconnects after quiet period expires.
// - All other modes: Shuts the WiFi radio off to prevent background 
//   networking tasks from lagging the user interface.
void manageWiFiState() {
  if (mode == 1) {
    if (millis() >= wifiQuietUntil && WiFi.getMode() == WIFI_OFF) {
      connectWiFi();
    }
  } else {
    if (WiFi.getMode() != WIFI_OFF) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  }
}

// Correctly wraps a long ZenQuote string into up to 4 lines of 23 characters max,
// without breaking words across lines. It skips leading/trailing quotes if present.
void wrapQuote(const String& quote, const String& author) {
  quoteLN1 = "";
  quoteLN2 = "";
  quoteLN3 = "";
  quoteLN4 = "";
  quoteLN5 = author;

  String temp = quote;
  temp.trim();
  
  // Strip external double quotes if present
  if (temp.startsWith("\"") && temp.endsWith("\"")) {
    temp = temp.substring(1, temp.length() - 1);
  }

  String lines[4] = {"", "", "", ""};
  int lineIdx = 0;
  
  while (temp.length() > 0 && lineIdx < 4) {
    // If the remaining quote text fits on one line, use it all
    if (temp.length() <= 23) {
      lines[lineIdx++] = temp;
      break;
    }
    
    // Find the last space within the first 23 characters to wrap nicely at a word boundary
    int lastSpace = temp.substring(0, 24).lastIndexOf(' ');
    if (lastSpace == -1) {
      // If no space exists in the first 23 characters, we are forced to cut the word
      lines[lineIdx++] = temp.substring(0, 23);
      temp = temp.substring(23);
    } else {
      // Split the line at the space
      lines[lineIdx++] = temp.substring(0, lastSpace);
      temp = temp.substring(lastSpace + 1); // Skip the space itself
    }
    temp.trim();
  }
  
  // Assign the formatted lines to the global rendering variables
  quoteLN1 = lines[0];
  quoteLN2 = lines[1];
  quoteLN3 = lines[2];
  quoteLN4 = lines[3];
}

// Safely fetches the current weather, local temperature, and timestamp from OpenWeatherMap
// and parses it using ArduinoJson. Supports integer temp responses cleanly.
void openWeatherFetch () {
  HTTPClient http;
  http.begin(endpoint + key); // Specify the URL
  int httpCode = http.GET();  // Make the GET request
  if (httpCode > 0) { 
    payload = http.getString();
    Serial.println(httpCode);
    Serial.println(payload);

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      // Retrieve and format the temperature
      if (doc.containsKey("main") && doc["main"].containsKey("temp")) {
        float tempVal = doc["main"]["temp"];
        temperature = String(tempVal, 1);
      }
      // Retrieve the short weather description
      if (doc.containsKey("weather") && doc["weather"].is<JsonArray>()) {
        JsonArray weatherArr = doc["weather"].as<JsonArray>();
        if (weatherArr.size() > 0) {
          description = weatherArr[0]["description"].as<String>();
        }
      }
      // Retrieve and format the weather report timestamp
      if (doc.containsKey("dt")) {
        long dtVal = doc["dt"];
        time_t rawtime = dtVal;
        ts = *localtime(&rawtime);
        strftime(buf, sizeof(buf), "%a %d-%m", &ts);
      }
    } else {
      Serial.print("Deserialization failed: ");
      Serial.println(error.c_str());
    }
  }
  else {
    Serial.println("Error on HTTP request");
  }
  http.end(); // Free the resources
}
  
// Fetches a random quote of the day from ZenQuotes API, parses the JSON safely
// using ArduinoJson, and formats it for our TFT layout.
void quotesFetch () {
  HTTPClient http;
  http.begin (zenQuotes); // Specify the URL
  int httpCode = http.GET(); // Make the request

  if (httpCode > 0) { 
    String s = http.getString();
    Serial.print("httpserver response:");Serial.println(httpCode);
    Serial.print("quote payload: ");Serial.println(s);

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, s);
    if (!error) {
      if (doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() > 0) {
          JsonObject firstQuote = arr[0].as<JsonObject>();
          String quote = firstQuote["q"].as<String>();
          String author = firstQuote["a"].as<String>();
          
          wrapQuote(quote, author); // Split text into lines
          
          Serial.print("quote: ");Serial.println(quote);
          Serial.print("auth: ");Serial.println(author);
        }
      }
    } else {
      Serial.print("Deserialization failed for quotes: ");
      Serial.println(error.c_str());
    }
  }
  http.end(); // Free the resources
}

void read_encoder() {
  static uint8_t lastStableState = 3;
  static uint8_t lastState = 3;
  static unsigned long lastStateTime = 0;
  static unsigned long lastCountTime = 0;
  const unsigned long settleTime = 250;
  const unsigned long fastThreshold = 50000;
  const int fastMultiplier = 3;

  uint8_t currentState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);

  if (currentState != lastState) {
    lastState = currentState;
    lastStateTime = micros();
  }
  else if (currentState != lastStableState && micros() - lastStateTime >= settleTime) {
    if (currentState == 3) {
      int change = 0;
      if (lastStableState == 1) change = 1;
      if (lastStableState == 2) change = -1;
      if (change != 0) {
        if (micros() - lastCountTime < fastThreshold) change *= fastMultiplier;
        counter += change;
        lastCountTime = micros();
        pauseWiFiFor(1500);
      }
    }
    lastStableState = currentState;
  }
}

void read_button() {
  static bool lastButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 100000; // 50ms in microseconds

  bool currentState = digitalRead(SWTCH);

  if (currentState != lastButtonState) {
    lastDebounceTime = micros();
    lastButtonState = currentState;
  }

  if (micros() - lastDebounceTime >= debounceDelay) {
    if (currentState == LOW) {  // button pressed (INPUT_PULLUP = LOW when pressed)
      if (mode == 1) {
        selectedSeconds = 20;
        timer = millis();
        magwait = millis();
        timeout = millis();
        tft.fillScreen(BgColour);
        ScrRefresh = 1;
        mode = 3;
        return;
      }
      else if (mode == 3) {
        digitalWrite(Light, LOW);
        digitalWrite(Mag, LOW);
        tft.fillScreen(BgColour);
        encoder = counter;
        timeout = millis();
        timer = millis();
        paused = 0;
        ScrRefresh = 1;
        mode = 6;
        return;
      }
      lastDebounceTime = micros() + 1000000; // 500ms lockout after trigger
    }
  }
}

void twodigitprint (int n,int x, int y) {

  if (n <=9) {
    tft.unloadFont();
    //tft.drawRect(x-21,y,45,30,TFT_RED);
    tft.drawNumber(0,x-30,y,7);
    tft.drawNumber(n,x,y,7);
  }
  else {
    //tft.drawRect(x-21,y,45,30,TFT_RED);
    tft.drawNumber(n,x-30,y,7);
  }
}

// Debounced door state reading (HIGH when door is open, LOW when closed)
bool readDoor() {
  static bool lastState = HIGH;
  static bool debouncedState = HIGH;
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50; // 50ms stable reading required

  bool currentState = digitalRead(Door);
  if (currentState != lastState) {
    lastDebounceTime = millis();
    lastState = currentState;
  }
  if (millis() - lastDebounceTime >= debounceDelay) {
    debouncedState = currentState;
  }
  return debouncedState;
}

void setup(void) {
  Serial.begin(115200);                                            // Serial Setup

  pinMode(ENC_A, INPUT_PULLUP);                                    // Set encoder pins and attach interrupts
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(SWTCH, INPUT_PULLUP);
  pinMode(Door, INPUT_PULLUP);
  pinMode(Light, OUTPUT);
  pinMode(Mag, OUTPUT);

  digitalWrite(Light,LOW);
  digitalWrite(Mag,LOW);
  
  //WIFI
  connectWiFi();
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }

  timeClient.begin();                                              // Clock Setup
  timeClient.setTimeOffset(-10800);

  tft.init();                                                      // Display Setup 
  tft.setRotation(1);
  tft.fillScreen(BgColour);
  tft.setTextColor(FgColour, BgColour);

  #pragma region                                                   // OTA
  ArduinoOTA                                                       // OTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";  // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if      (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  timeClient.update();
  quotesFetch();
  openWeatherFetch();
  refreshTime = millis();
  refreshWeather = millis();
  timeout = millis();

#pragma endregion  
}

void loop() {
  static int lastCounter = 0;
  buzzer.update();
  read_encoder();
  read_button();
  manageWiFiState();
  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
  door = readDoor();

  if (mode==0) { // ******************************** MODE 0 - Splash Screen ***************************
    digitalWrite(Light,LOW);
    digitalWrite(Mag,LOW);

    tft.drawString("DoraMeena", 16, 40,4);
    if (millis()-timeout>1500){ //if (millis() - timeout >= 5000); {
      tft.fillScreen(BgColour);
      timeout = millis();
      mode=1;
    }
  }

  if (mode==1) { // ******************************** MODE 1 - CLOCK ***********************************
    digitalWrite(Light,LOW);
    digitalWrite(Mag,LOW);

    if (millis() - refreshTimer >= scrRefresh*1000) {    // Update Display info
      ScrRefresh = 1;
      refreshTimer = millis();
    }    

    if (ScrRefresh == 1) {                                // Update Display Clock

      tft.fillRect(0, 70, 160, 16, BgColour);
      tft.drawCentreString(description,80,70,2);

      hh = (timeClient.getHours());
      mm = (timeClient.getMinutes());
      twodigitprint(mm,115,9);
      twodigitprint(hh,36,9);

      tft.fillRect(0, 102, 160, 16, BgColour);
      tft.drawString(buf,7,102,2);
      tft.drawRightString (temperature,143,102,2);
      tft.drawString("o",144,102,1);
    }

    if ( hh == 03 && mm == 01 ) {                             // Daily Reboot /old 11:11 easter egg
      delay(1000);
      ESP.restart();
      //digitalWrite(Light,HIGH);
    }

    if (timeClient.getSeconds() != ss) {                     // Update Display Flashing Colon
      ss = (timeClient.getSeconds());
      if (ss%2==0) {
        tft.drawString(":",70,14,7);
      }
      else {
        tft.setTextColor(BgColour, BgColour);
        tft.drawString(":",70,14,7);
        tft.setTextColor(FgColour, BgColour);
      }
    }
    if (counter != lastCounter){                             // On Encoder Events
      tone(Spkr,2300,4);
      if ( encoder < counter ) {
        ss = 0;
        mm = 0;
        hh = 0;
        twodigitprint(ss,115,9);
        twodigitprint(mm,36,9);
        mode=2;
        timeout = millis();
        return;
      }

      encoder = counter ;                                    // Reset encoder offset
      lastCounter = counter;
    }
    
    if (door == 1) {
      tft.fillScreen(BgColour);
      encoder = counter;
      timeout = millis();
      ScrRefresh = 1;
      mode = 4;
      return;
    }

    ScrRefresh = 0;
  }

  if (mode==2) { // ******************************** MODE 2 - TIME SELECT *****************************
    digitalWrite(Light,LOW);
    digitalWrite(Mag,LOW);

    if(counter != lastCounter){                              // On Encoder Events
      tone(Spkr,2300,4);
      timeout = millis();
      if (counter-encoder<0) {
        encoder = counter;
      }
      ss = (counter - encoder) %60;
      mm = ((counter - encoder) /60)%60;
      hh = ((counter - encoder) /60/60)%60;
      twodigitprint(ss,115,9);
      twodigitprint(mm,36,9);
      lastCounter = counter;
    }
    if (millis() - timeout >= AutoEnter*1000) {                   // Auto Enter
      if ((counter - encoder) >= 3) {
        selectedSeconds = (counter - encoder);
        mode=3;
        timer = millis();
        timeout = millis();
        magwait = millis();
        tft.fillScreen(BgColour);
        ScrRefresh = 1;
        return;
      }
      else {
        tft.fillScreen(BgColour);
        encoder = counter;
        timeout = millis();
        timer = millis();
        paused = 0;
        ScrRefresh = 1;
        mode = 6;       
        return;
      }
    }
  }

  if (mode==3) { // ******************************** MODE 3 - COOKING *********************************
    digitalWrite(Light,HIGH);
    if (millis() - magwait >= (Buffer*1000)) {
      digitalWrite(Mag,HIGH);
    }

    long countdown = (long)selectedSeconds - ((millis() - timer)/1000);
    if (countdown <= 0) {
      tft.fillScreen(BgColour);
      encoder = counter;
      timeout = millis();
      timer = millis();
      paused = 0;
      buzzer.start();
      ScrRefresh = 1;
      mode = 5;
      return;
    }
    
    if (countdown > 0 ) {                                    // While theres time to cook
      static long lastCountdown = -1;
      if (countdown != lastCountdown) {
        ss = (countdown )%60;
        mm = (countdown /60)%60;
        hh = (countdown /60/60)%60;
        twodigitprint(ss,115,9);
        twodigitprint(mm,36,9);
        tft.drawString(":",70,14,7);
        lastCountdown = countdown;
      }

      if (ScrRefresh == 1) {
        tft.drawString (quoteLN1,10,65,1);
        tft.drawString (quoteLN2,10,75,1);
        tft.drawString (quoteLN3,10,85,1);  
        tft.drawString (quoteLN4,10,95,1);
        tft.drawRightString (quoteLN5,145,110,1);
        ScrRefresh = 0;
      }

      paused = countdown;

      if(counter != lastCounter){
        tone(Spkr,2300,4);
        int change = counter - lastCounter;
        change = constrain(change, -1, 1);
        selectedSeconds = (long)selectedSeconds + change;
        timer = millis();
        lastCounter = counter;

        if ( (long)selectedSeconds <= 0 ) {
          tft.fillScreen(BgColour);
          encoder = counter;
          timeout = millis();
          timer = millis();
          paused = 0;
          ScrRefresh = 1;
          mode = 6;       
          return;
        }
      }
    paused = countdown;  // ← always update, unconditionally
    }

    if (door == 1) {                                         // Door Opens
      tft.fillScreen(BgColour);
      encoder = counter;
      timeout = millis();
      ScrRefresh = 1;
      mode = 4;
      return;
    }

  ScrRefresh = 0;  
  }
  if (mode==4) { // ******************************** MODE 4 - Door Open *******************************
    digitalWrite(Light,HIGH);
    digitalWrite(Mag,LOW);

    long countdown = (long)paused + counter - encoder;

    if (countdown <= 1 && paused > 0) {
      tft.fillScreen(BgColour);
      encoder = counter;
      timeout = millis();
      timer = millis();
      paused = 0;
      ScrRefresh = 1;
      mode = 6;
      return;
    }

    if (paused > 0) {
      if (ScrRefresh == 1) {
        tft.setTextColor(TFT_RED, BgColour);
        tft.drawCentreString("PAUSED", 80, 9, 4);
        tft.drawFastHLine(0, 35, 160, TFT_RED);
        
        tft.setTextColor(FgColour, BgColour);

        // Quote starting at y=65 (like Mode 3) since "OPEN DOOR" text was removed
        tft.drawString(quoteLN1, 10, 65, 1);
        tft.drawString(quoteLN2, 10, 75, 1);
        tft.drawString(quoteLN3, 10, 85, 1);
        tft.drawString(quoteLN4, 10, 95, 1);
        tft.drawRightString(quoteLN5, 145, 110, 1);
      }

      static int lastCounterInMode4 = -1;
      static int lastFlashSecond = -1;
      bool encoderMoved = (counter != lastCounterInMode4);
      bool flashOn = (millis() / 1000) % 2 == 0;
      char timeStr[6];
      sprintf(timeStr, "%02d:%02d", (int)(countdown / 60) % 60, (int)countdown % 60);

      if (encoderMoved || (int)(millis() / 1000) != lastFlashSecond || ScrRefresh == 1) {
        if (flashOn || encoderMoved) {
          tft.setTextColor(FgColour, BgColour);
          tft.drawCentreString(timeStr, 80, 40, 2);
        } else {
          tft.setTextColor(BgColour, BgColour);
          tft.drawCentreString(timeStr, 80, 40, 2);
          tft.setTextColor(FgColour, BgColour);
        }
        if (encoderMoved) {
          tone(Spkr, 2300, 4);
          timeout = millis(); // Reset timeout on encoder activity
        }
        lastCounterInMode4 = counter;
        lastFlashSecond = (int)(millis() / 1000);
      }
      
      if (ScrRefresh == 1) {
        ScrRefresh = 0;
      }
    }
    else {
      if (ScrRefresh == 1) {
        tft.setTextColor(TFT_RED, BgColour);
        tft.drawCentreString("OPEN DOOR", 80, 50, 4); // Center of the screen in Font 4
        ScrRefresh = 0;
      }
    }
    tft.setTextColor(FgColour, BgColour);

    if (door==0) {
      if (paused > 0) {
        selectedSeconds = countdown;
        timer = millis();
        timeout = millis();
        lastCounter = counter;
        tft.fillScreen(BgColour);
        ScrRefresh = 1;
        mode=3;
        return;
      }
      else {
        encoder = counter;
        timer = millis();
        timeout = millis();
        tft.fillScreen(BgColour);
        ScrRefresh = 1;
        mode=1;
        return;
      }
    }

    if (millis() - timeout >= 10000) {
      if (paused > 0) {
        encoder = counter;
        timeout = millis();
        timer = 0;
        paused = 0;
        tft.fillScreen(BgColour);
        ScrRefresh = 1;
        mode=6;
        return;
      }
    }
  }

  if (mode==5) { // ******************************** MODE 5 - Done Cooking ****************************
    digitalWrite(Light,HIGH);
    digitalWrite(Mag,LOW);

    if (ScrRefresh == 1) {
      tft.drawString("DONE !", 45, 20, 4);
      tft.drawString("ENJOY !", 35 , 80, 4);
      ScrRefresh = 0;
    }

    if (digitalRead(SWTCH) == LOW || counter != lastCounter) {
      buzzer.stop();
    }

    if (!buzzer.isPlaying()) {
      tft.fillScreen(BgColour);
      timeout = millis();
      ScrRefresh = 1;
      mode=1;
    }
  }

  if (mode==6) { // ******************************** MODE 6 - Canceled ********************************
    digitalWrite(Light,HIGH);
    digitalWrite(Mag,LOW);

    tft.drawString("Bye  Bye",32 ,46, 4);

    if (millis()-timeout>1000){
      if (door) {
        tft.fillScreen(BgColour);
        ScrRefresh = 1;
        mode=4;
      }
      else {
        tft.fillScreen(BgColour);
        timeout = millis();
        paused = 0;
        timer = 0;
        ScrRefresh = 1;      
        mode=1;
      }
    }
  } 

  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED && millis() - refreshTime > 3600000) { //********** TIME FETCH *************************************
    timeClient.update();
    ScrRefresh = 1;
    refreshTime = millis();
  }

  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED && millis() - refreshWeather > 600000) { //******** WEATHER FETCH **********************************
    quotesFetch();
    openWeatherFetch();
    ScrRefresh = 1;
    refreshWeather = millis();
  }

  if (Debug && millis()-debugtimer >= 300) {//****** DEBUG ********************************************
    Serial.print("timer "); Serial.print(timer); Serial.print(" ");
    Serial.print(" paused ");Serial.print(paused);
    //Serial.print(" door "); Serial.print(door); Serial.print(" ");
    Serial.print(" encoder "); Serial.print(encoder); Serial.print(" ");
    Serial.print(" counter "); Serial.print(counter); Serial.print(" ");
    //Serial.print("time "); Serial.print (timeClient.getHours()); Serial.print(" ");
    //Serial.print("date "); Serial.print (buf); Serial.print(" ");
    //Serial.print(" button "); Serial.print (button); Serial.print(" ");
    Serial.print(" ScrRefresh "); Serial.print (ScrRefresh); Serial.print(" ");
    Serial.print(" refreshTimer "); Serial.print (millis()-refreshTimer); Serial.print(" ");
    //Serial.print("button read 5"); Serial.print (analogRead(34)); Serial.print(" ");
    //Serial.print("temperature "); Serial.print (temperature); Serial.print(" ");
    //Serial.print("IP "); Serial.print(WiFi.localIP()); Serial.print(" ");
    Serial.print(" Mode "); Serial.println(mode);

    debugtimer = millis();
  }

}