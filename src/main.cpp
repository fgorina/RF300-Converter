
#include <EEPROM.h>
#include <ArduinoWebsockets.h>
#include "WiFi.h"
//#include <Time.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include <HTTPClient.h>
#include <ESPmDNS.h>

#include <driver/dac.h>

#define HELTEC

#ifdef HELTEC
  #include <heltec.h>
#endif

#define DEBUG false
#define DEBUG_1 true
#define DEBUG_ISR false

#define EEPROM_SIZE 512

#ifdef HELTEC
  #define IN_GPIO 13

  #define V_PWM 14
  #define ONBOARD_LED 25
#else
  #define IN_GPIO 16
  #define V_PWM 5
  #define ONBOARD_LED 2
#endif

#define TOUCH_GPIO 12
#define PWM_CHAN 0
#define AVG_SAMPLES 1000

// Wifi and SignalK Connections

#define metaUpdate "{\"updates\": [{\"meta\":[{\"path\":\"steering.rudderAngle\", \"value\": {\"units\": \"m\"}}]}]}"

#define update1 "{ \"context\": \""
#define update2 "\", \"updates\": [ {  \"source\": {\"label\": \"rudderAngleSensor\" }, \"values\": [ { \"path\": \"steering.rudderAngle\",\"value\":"
#define update4 " } ] } ]}"
#define update5 "\", \"updates\": [ {  \"source\": {\"label\": \"rudderAngleSensor\" }, \"values\": [ { \"path\": \"tanks.freshWater.1.currentLevel\",\"value\":"
#define update6 ", { \"path\": \"tanks.freshWater.1.currentVolume\",\"value\":"

char ssid[20] = "Yamato";
char password[20] = "ailataN1991";
char device_name[20] = "rudder";
char skserver[20] = "";
int skport = 0; // It is 4 bytes
char skpath[100] = "/signalk/v1/stream?subscribe=none";

// Frequency to angle conversion

const double center_f = 3000.0;
const double hz_degree = 20.0;
const double angle_range = 30.0; // +/- 30 degrees

unsigned long last = micros();
int samples = 0;
unsigned long ac_period = 0; 
double f = 0;
double angle = 0;
char buffer[256];

// Disable display

unsigned int touch_level = 30;
unsigned long timeout = 60000;  
unsigned long last_touched = 0;
bool display_on = true;



using namespace websockets;

bool mdnsDone = false; // Will be true when we have a server

bool wifi_connect = false;
int enabled = 0; // 0 Deshabilita les accions fins que s'ha rebut un command
WebsocketsClient client;
int socketState = -5; // Change to -4 if want connect -5 does not use WiFi, -4 -> Before connecting to WiFi, -3, -2.Connection authorized, 2-> Connected and authorized

String me = "vessels.self";
char token[256] = "";
char bigBuffer[1024] = "";


#include "level.h"


TaskHandle_t task_pres;
TaskHandle_t task_empty;
TaskHandle_t taskNetwork;


int ledState = 0;
int ledOn = 0;
int ledOff = 100;

// Function Prototypes


void IRAM_ATTR ISR()
{  
  #if DEBUG_ISR
    Serial.print(".");
  #endif



  unsigned long m = micros();
  unsigned long period = m - last;
  ac_period += period;
  samples += 1;
  last = m;
}

// LEDs

void clearLed()
{
  ledState = 0;
  digitalWrite(ONBOARD_LED, ledState);
}

void setLed()
{
  ledState = 1;

  digitalWrite(ONBOARD_LED, ledState);
}

void toggleLed()
{
  if (ledState == 0)
  {
    ledState = 1;
  }
  else
  {
    ledState = 0;
  }

  digitalWrite(ONBOARD_LED, ledState);
}



void ledTask(void *parameter)
{

  for (;;)
  {
    if (ledOn > 0)
    {
      setLed();
      vTaskDelay(ledOn);
    }

    if (ledOff > 0)
    {
      clearLed();
      vTaskDelay(ledOff);
    }
  }
}

#include "signalk.h"


// Sets output voltage proportional to the rudder angle
// For testing uses DAC and PWM

void setVoltage(double v) {
         
  unsigned int i = floor(v * 1024.0);
  ledcWrite(PWM_CHAN, i);

}

// This writes the screen and computes frequency ajd angle.
void presentationTask(void *parameter)
{
  float f = 0.0;
  while (true)
  {
    if (samples >= AVG_SAMPLES)
    {
      int oldsamples = samples;
      unsigned long old_ac_period = ac_period;

      double period = 2.0 * double(ac_period) / double(samples);
      samples = 0;
      ac_period = 0;

      f = double(1000000) / double(period);

      if(DEBUG_1){
        Serial.print("Ac Period "); Serial.print(old_ac_period); Serial.print(" Samples "); Serial.print(oldsamples); Serial.print(" f "); Serial.println(f);
      }

      double delta = f - center_f;
      angle = round(delta / hz_degree);
      double rads = angle / 180.0 * PI;
      sendData(rads);

      // -30.0 es 0, 30.0 es 255

      double v = (angle + angle_range) / (2 * angle_range);
      v = max(min(1.0, v), 0.0);
      if(DEBUG_1){
        Serial.print("Angle ");Serial.print(angle);Serial.print(" Voltage "); Serial.println(v);
      }
      setVoltage(v);

    }

      #ifdef HELTEC
        Heltec.display->clear();

        sprintf(buffer, "f: %0.f Hz", round(f));  
        Heltec.display->drawString(0, 0, String(buffer));

        sprintf(buffer, "a: %0.f º", angle);
        Heltec.display->drawString(0, 20, String(buffer));

        if(socketState == -5){
          Heltec.display->drawString(0, 40, "Not Connected");
        }else if(socketState == -4){
          Heltec.display->drawString(0, 40, "Disconnected");
        }
        else if(socketState == -3){
          Heltec.display->drawString(0, 40, "Connecting");
        }else if(socketState == -2){
          Heltec.display->drawString(0, 40, "WiFi OK");
        }else if(socketState == 0){
          Heltec.display->drawString(0, 40, "SK OK");
        }else if(socketState == 2){
          Heltec.display->drawString(0, 40, "SK Authd");
        }else{
          sprintf(buffer , "SS %d", socketState);
          Heltec.display->drawString(0, 40, buffer);
        }
        Heltec.display->display(); //
      #endif
    vTaskDelay(50); // May be adjusted for necessity 
  }
}


// Runs the on board led to show connection status to signalk
void emptyTask(void *parameter)
{
  while (true)
  {

    if (ledOn > 0)
    {
      setLed();
      vTaskDelay(ledOn);
    }

    if (ledOff > 0)
    {
      clearLed();
      vTaskDelay(ledOff);
    }
  }
}


  // Load Data from EEPROM
void loadEEPROM(){

  float f1;
  float f2;
  char s20[20];

  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(0, f1);
  EEPROM.get(4, f2);

  if (isnan(f1) || isnan(f2) || f1 == 0.0 || f2 == 0.0 )
  {

    f1 = 1.0;
    f2 = 1.0;
    // Init EEPROM Area
    EEPROM.put(0, f1);
    EEPROM.put(4, f2);
    EEPROM.put(8, ssid);
    EEPROM.put(28, password);
    EEPROM.put(48, device_name);
    EEPROM.put(68, skserver);
    EEPROM.put(88, skport);
    EEPROM.put(92, skpath);
    EEPROM.put(192, token);

    EEPROM.commit();
    if (DEBUG || true)
    {
      Serial.println();
    }
    if (DEBUG || true)
    {
      Serial.println("Written default data to EEPROM");
    }
  }
  else
  {

    if (DEBUG || true)
    {
      Serial.println("EEPROM already vith values");
    }
  }

  EEPROM.get(8, ssid);
  EEPROM.get(28, password);
  EEPROM.get(48, s20);

  if (strlen(s20) != 0)
  {
    strcpy(device_name, s20);
  }

  EEPROM.get(68, skserver);
  EEPROM.get(88, skport);
  EEPROM.get(92, skpath);
  EEPROM.get(192, token);

  if (strlen(skserver) > 0)
  {
    Serial.println("Alreaddy have a server, no need to lookup by mDns");
    mdnsDone = true;
  }
}
void setup()
{

  Serial.begin(115200);
  
  pinMode(IN_GPIO, INPUT);
  ledcSetup(PWM_CHAN, 10000, 10);
  ledcAttachPin(V_PWM, PWM_CHAN);

  //loadEEPROM();

#ifdef HELTEC
  Heltec.begin(true, false, true);
  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->setFont(ArialMT_Plain_24);

#endif
 

  sprintf(buffer, "a: %0.f º %0.2f r", 0.0, 0.0);
  xTaskCreatePinnedToCore(presentationTask, "Presentation", 4000, NULL, 1, &task_pres, 0);
  xTaskCreatePinnedToCore(emptyTask, "Empty", 4000, NULL, 1, &task_empty, 1);
  xTaskCreatePinnedToCore(networkTask, "TaskNetwork", 4000, NULL, 1, &taskNetwork, 0);
 
  attachInterrupt(IN_GPIO, ISR, CHANGE);

  last_touched = millis();


  if(DEBUG_1){
    Serial.print("Center f ");Serial.print(center_f);Serial.print(" Hz, Step "); Serial.print(hz_degree);Serial.print(" Hz/deg"); 
  }


}
void loop()
{
  unsigned long t = millis();
  if ((t - last_touched) > timeout && display_on){

    if(DEBUG){
      Serial.println("Display Off)");
    }
    Heltec.display->displayOff();
    display_on = false;  
  }


  if(touchRead(TOUCH_GPIO) < touch_level){

    if(!display_on){
      Heltec.display->displayOn();
      display_on = true;
    }
    last_touched = millis();
  }
  
  vTaskDelay(10);
}
