#ifndef LEVEL_H
#define LEVEL_H

#ifdef __cplusplus
extern "C" {
#endif



#define TRIGGER_GPIO 12
#define END_GPIO 14

// Tank definition. Sizes in m

#define MAX_DISTANCE_SAMPLES 300

double height = 0.45; // 80 Gal
double width = 0.29;  // 80 Gal
double length = 0.24; // 80 Gal
const double sound_speed = 340.0;
float level = 0.5; // %0% Full

double distance_samples[MAX_DISTANCE_SAMPLES];
int sample_pointer = 0;

long last_measure = 0;

double max_height_time = height * 2.0 / sound_speed * 1e6;

unsigned long micros_up = 0;   // When ultrasound goes up
unsigned long micros_down = 0; // when ultrasound goes down
double ac_distance = 0.0;

// Kalman filter for tank level
// All units are fractions

// x_predicted == 0 -> inici
// First measure is not processed
// but initializes x_predicted, x_filtered

double x_predicted = 0.0;
double p_predicted = 1.0;
double K = 0.0;
double x_filtered = 0.0;
double p_computed = 1.0;
double r_measure = 0.001;

long watchdog = 0;

void IRAM_ATTR ISR_DISTANCE()
{

  unsigned long m = micros();
  if (micros_up == 0)
  {
    micros_up = m;
  }
  else
  {
    micros_down = m;
  }
}

void sendTrigger()
{
  // First set temps = 0 to start
  micros_up = 0;
  micros_down = 0;

  // Send trigger pulse

  digitalWrite(TRIGGER_GPIO, 0);
  vTaskDelay(2);
  digitalWrite(TRIGGER_GPIO, 1);
  vTaskDelay(10);
  digitalWrite(TRIGGER_GPIO, 0);
}

void sendLevel(double d)
{

  level = d;
  double volume = level * (height * width * length);

  if (level < 0.0 || level > 1.0)
  {
    Serial.print("Error a mesura ");
    Serial.println(d);
    return;
  }

  if (socketState != 2 && DEBUG)
  {
    Serial.println("sendData called when not connected to server");
    return;
  }

  String s = update1 + me + update5 + String(level) + "}" + update6 + String(volume) + update4;
  client.send(s);
  if (DEBUG_1)
  {
    Serial.print("Sent: ");
    Serial.println(s);
  }
}


void kalman_step(double value)
{
  x_predicted = x_filtered;
  p_predicted = p_computed;
  K = p_predicted / (p_predicted + r_measure);
  x_filtered = x_predicted + K * (value - x_predicted);
  p_computed = (1.0 - K) * p_predicted;
}

void distanceTask(void *parameter)
{
  long ic = 0;

  while (true)
  {

    watchdog = millis();
    sendTrigger();

    while (micros_down == 0 && (millis() - watchdog) < 1000) // Don't have an answer
    {
      vTaskDelay(1);
    }

    if (millis() - watchdog < 1000)
    {

      long delta = micros_down - micros_up;
      if (delta <= max_height_time)
      {

        double distance = delta * 1e-6 * sound_speed / 2.0;
        double h = height - distance;
        double t_level = h / height;

        if (x_predicted == 0)
        {
          x_predicted = t_level;
          x_filtered = t_level;
        }
        else
        {
          kalman_step(t_level);
        }

        if (DEBUG_1)
        {
          Serial.print(t_level);
          Serial.print(" ");
          Serial.println(x_filtered);
        }

        /*int to_remove = (sample_pointer +1) % MAX_DISTANCE_SAMPLES;
        double old_value = distance_samples[to_remove];
        ac_distance += (distance - old_value);
        distance_samples[sample_pointer] = distance;
        sample_pointer = to_remove;
        */
      }
    }
    else
    {
      Serial.println("Timeout");
    }
    ic += 1;
    ic = ic % 100;

    if (ic == 0)
    {
      // double avg_distance = ac_distance / double(MAX_DISTANCE_SAMPLES);

      sendLevel(x_filtered);
      vTaskDelay(500);
      // Serial.print("Distance : ");
      // Serial.println(avg_distance * 100.0);
    }
  }
}


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif