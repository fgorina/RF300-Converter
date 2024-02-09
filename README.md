# A module for connecting Simrad RF300 Rudder feedback to PyPilot and SignalK

## THIS IS A TEST PROJECT!!!  


Actual source also includes code for a tank level with ultrasounds. Please, just disregard it.

Also there is a lot of code for connecting to signalk that may be ommitted.

Most of the tank code is in level.h and most of signalk code in signalk.h 

Pending to create classes for these tasks (jusat a project for tesing here).

So for the conversion of frquency to voltage and reading the angle, all the code is in main.cpp and esentially in :

void IRAM_ATTR ISR() 
    - Detects changes in input value and stores total time and number of cycles
    - Triggered with up and down transitions so will get double number of cycles

void presentationTask(void *parameter)
    - Executes periodically
    - Computes frequency and angle from acumulated samples and total time
    - Converts frequency to angle
    - Converts angle to voltage suposing +/- 35º full rudder course
    - Generates voltage with the DAC and the PWM at 10kHz for test and comparisons
    - Displays data in WifiKit32 OLED display for debugging. Remove all heltec routines
        if working with another device 

void setup()
    - Initializes all. For rudder angle just in GPIO and output GPIO form voltage
    - Generates internally the waveform at the GENERATOR pin so we may test it without the device or function generator.
    - Creates the tasks and sets them running
    - Attaches interrupts
    - Starts frquency genrator for tests


void loop()
    - Just increases and decreases simulated rudder angle from -30 to 30



PINS

    GPIO 13 - Input frequency signal
    GPIO 27 - Frequency Generator
    GPIO 26 - DAC output
    GPIO 17 - PWM output

    For the tank level meter

    GPIO 12 - Trigger 
    GPIO 14 - Output from transducer. Pulse length proportional to distance

