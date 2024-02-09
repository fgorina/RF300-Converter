#include <heltec.h>
#include <EEPROM.h>
#include <ArduinoWebsockets.h>
#include "WiFi.h"
#include <Time.h>
#include <ArduinoJson.h>
#include <ArduinoJson.hpp>
#include <HTTPClient.h>
#include <ESPmDNS.h>


#define DEBUG false
#define DEBUG_1 false

#define EEPROM_SIZE 512

#define ONBOARD_LED 25
#define IN_GPIO 13

#define GENERATOR 27
#define V_OUT 26
#define V_PWM 17
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

const float center_f = 3400.0;
unsigned long last = micros();
int samples = 0;
unsigned long ac_period = 0; 
char buffer[256];

// Some data for the frequency generator

unsigned long sg_period = floor(1000000.0 / center_f / 2.0);
int sg_state = 0;
hw_timer_t *generator_timer = NULL;

using namespace websockets;

bool mdnsDone = false; // Will be true when we have a server

bool wifi_connect = false;
int enabled = 0; // 0 Deshabilita les accions fins que s'ha rebut un command
WebsocketsClient client;
int socketState = -4; // -5 does not use WiFi, -4 -> Before connecting to WiFi, -3, -2.Connectingauthorized, 2-> Connected and authorized

String me = "vessels.self";
char token[256] = "";
char bigBuffer[1024] = "";


#include "level.h"


TaskHandle_t task_pres;
TaskHandle_t task_empty;
TaskHandle_t taskNetwork;
TaskHandle_t taskDistance;

int ledState = 0;
int ledOn = 0;
int ledOff = 100;

// Function Prototypes


void IRAM_ATTR ISR()
{

  unsigned long m = micros();
  unsigned long period = m - last;
  ac_period += period;
  samples += 1;
  last = m;
}



void IRAM_ATTR ISR_GENERATOR()
{

  digitalWrite(GENERATOR, !digitalRead(GENERATOR));
  /*if(sg_state){
    REG_WRITE(GPIO_OUT_W1TS_REG, BIT27);
  }else{
    REG_WRITE(GPIO_OUT_W1TC_REG, BIT27);
  }
  sg_state = !sg_state;
*/
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

void setVoltage(float v) {
 
  unsigned int i = floor(v * 1024.0);
  dacWrite(V_OUT, i);
  ledcWrite(PWM_CHAN, i);

}

// This writes the screen and computes frequency ajd angle.
void presentationTask(void *parameter)
{
  while (true)
  {
    if (samples >= AVG_SAMPLES)
    {
      float period = 2.0 * float(ac_period) / float(samples);
      samples = 0;
      ac_period = 0;

      float f = float(1000000) / float(period);
      Heltec.display->clear();

      float delta = f - center_f;
      float angle = delta / 20.0;
      float rads = angle / 180.0 * PI;
      sendData(rads);

      // -30.0 es 0, 30.0 es 255

      float v = (angle + 35.0) / 70.0;
      setVoltage(v);
      sprintf(buffer, "f: %0.f Hz", f);
      Heltec.display->drawString(0, 0, String(buffer));

      sprintf(buffer, "a: %0.f º %0.2f r", angle, rads);
      Heltec.display->drawString(0, 20, String(buffer));
      Heltec.display->display(); //

      // Here is for the level meter

      sprintf(buffer, "level %0.2f %", level);
      Heltec.display->drawString(0, 40, String(buffer));
      Heltec.display->display(); //
    }
    vTaskDelay(50);
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

// Connection data to SignalK
void loadEEPROM()
{
  // Load Data from EEPROM

  float f1;
  float f2;
  char s20[20];

  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(0, f1);
  EEPROM.get(4, f2);

  if (isnan(f1) || isnan(f2) || f1 == 0.0 || f2 == 0.0)
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
  // put your setup code here, to run once:

  Heltec.begin(true, false, true);

  Serial.begin(115200);
  
  pinMode(IN_GPIO, INPUT);

  pinMode(END_GPIO, INPUT);
  pinMode(TRIGGER_GPIO, OUTPUT);


  pinMode(GENERATOR, OUTPUT);

  ledcSetup(PWM_CHAN, 10000, 10);
  ledcAttachPin(V_PWM, PWM_CHAN);

  for (int i = 0; i < MAX_DISTANCE_SAMPLES; i++)
  {
    distance_samples[i] = 0.0;
  }

  loadEEPROM();

  Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
  Heltec.display->setFont(ArialMT_Plain_24);

  xTaskCreatePinnedToCore(presentationTask, "Presentation", 4000, NULL, 1, &task_pres, 0);
  xTaskCreatePinnedToCore(emptyTask, "Empty", 4000, NULL, 1, &task_empty, 1);
  xTaskCreatePinnedToCore(networkTask, "TaskNetwork", 4000, NULL, 1, &taskNetwork, 0);
  xTaskCreatePinnedToCore(distanceTask, "DistanceNetwork", 6000, NULL, 1, &taskDistance, 0);

  attachInterrupt(IN_GPIO, ISR, CHANGE);
  attachInterrupt(END_GPIO, ISR_DISTANCE, CHANGE);

 

  sg_period = floor(1000000.0 / (center_f  ) / 2.0);

  generator_timer = timerBegin(1, 80, true);
  timerAttachInterrupt(generator_timer, &ISR_GENERATOR, true);
  timerAlarmWrite(generator_timer, sg_period , true);
  timerAlarmEnable(generator_timer);
}

float mangle = -30.0;
float delta = 1.0;

void loop()
{

  Serial.print("Rudder to "); Serial.println(mangle);

  sg_period = floor(1000000.0 / (center_f + (mangle * 20.0)) / 2.0);
  timerAlarmWrite(generator_timer, sg_period , true);
  mangle += delta;

  if (mangle > 30.0){
    delta = - delta;
  }

  if (mangle < -30.0){
    delta = -delta;
  }

  vTaskDelay(1000);
}
