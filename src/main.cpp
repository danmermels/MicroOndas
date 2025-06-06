#include <Arduino.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip.
#include <SPI.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
//#include <ArduinoJson.h>
#include <ArduinoOTA.h>
//#include <ESPmDNS.h>
#include "pitches.h"
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

unsigned int refreshTime = 30000;
unsigned int refreshWeather = 30000;

int melody[] = {                                           // notes in the melody:
  NOTE_C4, NOTE_G3, NOTE_G3, NOTE_A3, NOTE_G3, 0, NOTE_B3, NOTE_C4
};
int noteDurations[] = {                                    // note durations: 4 = quarter note, 8 = eighth note, etc.:
  4, 8, 8, 4, 4, 4, 4, 4
};

String payload = ""; //weather

unsigned long timer = 0;           // Cooking time
unsigned long timeout = 0;         // Menu Timer
unsigned long paused = 0;          // Cooking time at Door Open
unsigned long magwait = 1;         // Delay Mag
unsigned long refreshTimer = 0;    // Screen refresh timer
unsigned long debugtimer = 1;      // Debug timer
bool door;                         // Door Status
bool ScrRefresh = 1;               // Screen rfsh Status

unsigned int mode = 0;             //Program mode
unsigned int debounce = 0;         //Debouncer

unsigned long _lastIncReadTime = micros();
unsigned long _lastDecReadTime = micros();
unsigned int _pauseLength = 25000;
unsigned int _fastIncrement = 10;

volatile int counter = 0; // Absolute Encoder position
volatile int encoder = 1; // Relative Encoder position
volatile int Button = 0; // Relative Encoder position

uint8_t hh=00, mm=00, ss=00;  //  H, M, S Declaration

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

void openWeatherFetch () {
    HTTPClient http;
    http.begin(endpoint + key); //Specify the URL
    int httpCode = http.GET();  //Make the request
    if (httpCode > 0) { //Check for the returning code
      payload = http.getString();
      Serial.println(httpCode);
      Serial.println(payload);
      }
    else {
      Serial.println("Error on HTTP request");
      }

    http.end(); //Free the resources

    //DynamicJsonDocument jsonBuffer(1024);
    //DeserializationError error = deserializeJson(jsonBuffer, payload);
    //if (error) {
    //  Serial.print("Deserialization failed with code: ");
    //  Serial.println(error.c_str());
   //   return;
   //   }
   // JsonArray array = jsonBuffer["weather"].as<JsonArray>();
    //temperature = (float)(jsonBuffer["main"]["temp"]);
    data = payload.substring(payload.indexOf("dt")+4 , payload.indexOf("dt")+14);
    temperature = payload.substring(payload.indexOf("temp")+6 , payload.indexOf("." , payload.indexOf("temp"))+2);
    description = payload.substring(payload.indexOf("description")+14 , payload.indexOf("," , payload.indexOf("description"))-1);

    //Serial.println(description);
    const char* charArray = data.c_str(); //(jsonBuffer["dt"]); //

    //rawtime = rawtime -10800;
    time_t rawtime = atol(charArray);
    ts = *localtime(&rawtime);
    strftime(buf, sizeof(buf), "%a %d-%m", &ts);
    printf("%s\n", buf);
    //Serial.print("rawtime: ");
    //Serial.print(rawtime);
  }
  
void quotesFetch () {
  HTTPClient http;
  http.begin (zenQuotes); //Specify the URL
  int httpCode = http.GET(); //Make the request

  if (httpCode > 0) {//Check for the returning code 
    String s = http.getString();
    int firstcolon = s.indexOf(":");
    int seccolon = s.indexOf(":",firstcolon+1);
    int thirdcolon = s.indexOf(":",seccolon+1);
    String quote = s.substring(firstcolon+1 , seccolon-4);
    String author = s.substring(seccolon+2,thirdcolon-5);
    
    quoteLN1 = quote.substring(0,23).substring(0,quote.lastIndexOf(" "));
    quoteLN2 = quote.substring(23,46).substring(0,quote.lastIndexOf(" "));;
    quoteLN3 = quote.substring(46,69).substring(0,quote.lastIndexOf(" "));;
    quoteLN4 = quote.substring(69,92);
    quoteLN5 = author;

    Serial.print("httpserver response:");Serial.println(httpCode);
    //Serial.print("payload content:");Serial.println(s);
    Serial.print("quote: ");Serial.println(quote);
    Serial.print("auth: ");Serial.println(author);

    http.end(); //Free the resources

   }
}

void read_encoder() {
  // Encoder interrupt routine for both pins. Updates counter
  // if they are valid and have rotated a full indent

  static uint8_t old_AB = 3;  // Lookup table index
  static int8_t encval = 0;   // Encoder value
  static const int8_t enc_states[]  = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0}; // Lookup table

  old_AB <<=2;  // Remember previous state

  if (digitalRead(ENC_A)) old_AB |= 0x02; // Add current state of pin A
  if (digitalRead(ENC_B)) old_AB |= 0x01; // Add current state of pin B

  encval += enc_states[( old_AB & 0x0f )];

  // Update counter if encoder has rotated a full indent, that is at least 4 steps
  if( encval > 3 ) {        // Four steps forward
    int changevalue = -1;
    if((micros() - _lastIncReadTime) < _pauseLength) {
      changevalue = _fastIncrement * changevalue;
    }
    _lastIncReadTime = micros();
    counter = counter + changevalue;              // Update counter
    encval = 0;
  }
  else if( encval < -3 ) {        // Four steps backward
    int changevalue = 1;
    if((micros() - _lastDecReadTime) < _pauseLength) {
      changevalue = _fastIncrement * changevalue;
    }
    _lastDecReadTime = micros();
    counter = counter + changevalue;              // Update counter
    encval = 0;
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

/*
void button_press() {
  if (millis() - debounce >= 2000) {
    if (mode==1) {
      //encoder = counter - 60;
      //mode = 2;
    }
    else if (mode==2) {
      digitalWrite(Light,LOW);
      digitalWrite(Mag,LOW);
      timeout = millis();
      timer = millis();
      paused = 0;
      //tft.fillScreen(FgColour);
      mode = 6;
    }
    else if (mode==3) {
      digitalWrite(Light,LOW);
      digitalWrite(Mag,LOW);

      encoder = counter;
      timeout = millis();
      timer = millis();
      paused = 0;
      //tft.fillScreen(FgColour);
      mode = 6;
    }
    else if (mode==4) {
      digitalWrite(Light,LOW);
      digitalWrite(Mag,LOW);

      encoder = counter;
      timeout = millis();
      timer = millis();
      paused = 0;
      //tft.fillScreen(FgColour);
      mode = 6;
    }
    Button = 0;
    debounce = millis();
  }
}
*/

void setup(void) {
  pinMode(ENC_A, INPUT_PULLUP);                                    // Set encoder pins and attach interrupts
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(SWTCH, INPUT_PULLUP);
  pinMode(Door, INPUT_PULLUP);
  pinMode(Light, OUTPUT);
  pinMode(Mag, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(ENC_A), read_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), read_encoder, CHANGE);

  digitalWrite(Light,LOW);
  digitalWrite(Mag,LOW);
  
  WiFi.mode(WIFI_STA);                                             // WiFi Start
  WiFi.begin(ssid, password);
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

  Serial.begin(115200);                                            // Serial Setup

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
  timeout = millis();
#pragma endregion  
}

void loop() {
  static int lastCounter = 0;
  ArduinoOTA.handle();
  door = digitalRead(Door);

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

      tft.drawCentreString(description,80,70,2);

      hh = (timeClient.getHours());
      mm = (timeClient.getMinutes());
      twodigitprint(mm,115,9);
      twodigitprint(hh,36,9);

      tft.drawString(buf,7,102,2);
      tft.drawRightString (temperature,143,102,2);
      tft.drawString("o",144,102,1);
    }

    if ( hh == 03 && mm == 01 ) {                             // Daily Reboot /old 11:11 easter egg
      delay(1000);
      ESP.restart();
      //digitalWrite(Light,HIGH);
      delay(1000);
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
      }

      encoder = counter ;                                    // Reset encoder offset
      lastCounter = counter;
    }
    
    if (door == 1) {
      tft.fillScreen(BgColour);
      tft.drawString("OPEN DOOR",30,16,7);
      encoder = counter;
      timeout = millis();
      mode = 4;
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
      tft.drawString(":",70,14,7);
      lastCounter = counter;
    }
    if (millis() - timeout >= AutoEnter*1000) {                   // Auto Enter
      if (ss >= 3) {
        mode=3;
        timer = millis();
        timeout = millis();
        magwait = millis();
        tft.fillScreen(BgColour);
      }
      else {
        tft.fillScreen(BgColour);
        encoder = counter;
        timeout = millis();
        timer = millis();
        paused = 0;
        mode = 6;       
      }
    }
  }

  if (mode==3) { // ******************************** MODE 3 - COOKING *********************************
    digitalWrite(Light,HIGH);
    if (millis() - magwait >= (Buffer*1000)) {
      digitalWrite(Mag,HIGH);
    }

    int countdown = (counter - encoder) - ((millis() - timer)/1000);
    
    if (countdown > 0 ) {                                    // While theres time to cook
      ss = (countdown )%60;
      mm = (countdown /60)%60;
      hh = (countdown /60/60)%60;
      twodigitprint(ss,115,9);
      twodigitprint(mm,36,9);

      tft.drawString(":",70,14,7);
      //tft.setTextWrap(1,0);
      tft.drawString (quoteLN1,10,65,1);
      tft.drawString (quoteLN2,10,75,1);
      tft.drawString (quoteLN3,10,85,1);  
      tft.drawString (quoteLN4,10,95,1);
      tft.drawRightString (quoteLN5,145,110,1);

      paused = countdown;

      if(counter != lastCounter){                            // On Encoder Events
        tone(Spkr,2300,4);
        if ( ss <= 8 ) {                                     // User Cancel
        tft.fillScreen(BgColour);
        encoder = counter;
        timeout = millis();
        timer = millis();
        paused = 0;
        mode = 6;       
        }
        lastCounter = counter;
      }
    }

    else {                                                   // When time runs out
      tft.fillScreen(BgColour);
      encoder = counter;
      timeout = millis();
      timer = millis();
      paused = 0;
      mode = 5;
    }

    door = digitalRead(Door);

    if (door == 1) {                                         // Door Opens
      
      tft.fillScreen(BgColour);
      tft.drawString("OPEN DOOR",30,76,7);
      encoder = counter;
      timeout = millis();
      mode = 4;
    }

  ScrRefresh = 0;  
  }

  if (mode==4) { // ******************************** MODE 4 - Door Open *******************************
      digitalWrite(Light,HIGH);
      digitalWrite(Mag,LOW);

    int countdown = paused + counter - encoder;

    if (countdown > 8 ) {
      ss = (countdown )%60;
      mm = (countdown /60)%60;
      hh = (countdown /60/60)%60;
      twodigitprint(ss,115,9);
      twodigitprint(mm,36,9);
      tft.drawString(":",70,14,7);
    }

      tft.setTextColor(TFT_RED, BgColour);
      tft.drawString("OPEN DOOR", 5, 80, 4);
      
      if (door==0) {
        tft.setTextColor(FgColour, BgColour);

        if (paused > 0) {
          encoder = counter - countdown;
          timer = millis();
          timeout = millis ();
          tft.fillScreen(BgColour);
          ScrRefresh = 1;        
          mode=3;
        }

        else {
          encoder = counter;
 
          timer = millis();
          timeout = millis ();
          tft.fillScreen(BgColour);
          ScrRefresh = 1;
          mode=1;
        }
      }  
      
      if (millis() - timeout >= 10000){
        if (paused > 0) {
          encoder = counter;
          timeout = millis();
          timer = 0;
          paused = 0;
          tft.fillScreen(BgColour);
          ScrRefresh = 1;        
          mode=6;
        }
      }

    door = digitalRead(Door);
  }

  if (mode==5) { // ******************************** MODE 5 - Done Cooking ****************************
    digitalWrite(Light,HIGH);
    digitalWrite(Mag,LOW);

    ScrRefresh = 1;

    tft.drawString("DONE !", 45, 20, 4);
    tft.drawString("ENJOY !", 35 , 80, 4);

  // iterate over the notes of the melody:
  for (int thisNote = 0; thisNote < 8; thisNote++) {

    // to calculate the note duration, take one second divided by the note type.
    //e.g. quarter note = 1000 / 4, eighth note = 1000/8, etc.
    int noteDuration = 1000 / noteDurations[thisNote];
    tone(25, melody[thisNote], noteDuration);

    // to distinguish the notes, set a minimum time between them.
    // the note's duration + 30% seems to work well:
    int pauseBetweenNotes = noteDuration * 1.0;
    delay(pauseBetweenNotes);
    // stop the tone playing:
    noTone(Spkr);
  }

    if (millis()-timeout>1000){
      tft.fillScreen(BgColour);
      noTone(Spkr);
      timeout = millis();
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

  if (millis() - refreshTime > 3600000) { //********** TIME FETCH *************************************
    timeClient.update();
    ScrRefresh = 1;
    refreshTime = millis();
    }

  if (millis() - refreshWeather > 600000) { //******** WEATHER FETCH **********************************
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
  Serial.print(" button "); Serial.print (Button); Serial.print(" ");
  Serial.print(" ScrRefresh "); Serial.print (ScrRefresh); Serial.print(" ");
  Serial.print(" refreshTimer "); Serial.print (millis()-refreshTimer); Serial.print(" ");
  //Serial.print("button read 5"); Serial.print (analogRead(34)); Serial.print(" ");
  //Serial.print("temperature "); Serial.print (temperature); Serial.print(" ");
  //Serial.print("IP "); Serial.print(WiFi.localIP()); Serial.print(" ");
  Serial.print(" Mode "); Serial.println(mode);

  debugtimer = millis();
  }

}