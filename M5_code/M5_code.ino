#include <Arduino.h>
#include <ESP_FlexyStepper.h>
#include <stdio.h>

// Stepper motor pin definitions
const int ENABLE_PIN = 4;    // shared enable (active-low)
const int STEP_PIN_X = 16;   // X-axis STEP
const int DIR_PIN_X  = 17;   // X-axis DIR
const int STEP_PIN_Y = 12;   // Y-axis STEP
const int DIR_PIN_Y  = 13;   // Y-axis DIR

// Home switch pins (assumed active-low)
const int HOME_SWITCH_X = 25;
const int HOME_SWITCH_Y = 26;

// Motion configuration
const float ACCELERATION = 800.0f;   // steps/s^2 for normal moves
const float TOP_SPEED    = 2000.0f;  // steps/s
const float HOMING_SPEED = 800.0f;   // steps/s during homing
const long  HOMING_MAX_DISTANCE = 50000; // max steps to search for switch

// Conversion from centimeters to motor steps
const float STEPS_PER_CM = 100.0f;

ESP_FlexyStepper stepperX;
ESP_FlexyStepper stepperY;

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

// Home both axes at startup with zero acceleration
void homeStartup() {
    stepperX.setAccelerationInStepsPerSecondPerSecond(0);
    stepperY.setAccelerationInStepsPerSecondPerSecond(0);
    stepperX.moveToHomeInSteps(-1, HOMING_SPEED, HOMING_MAX_DISTANCE, HOME_SWITCH_X);
    stepperY.moveToHomeInSteps(-1, HOMING_SPEED, HOMING_MAX_DISTANCE, HOME_SWITCH_Y);
    stepperX.setCurrentPositionInSteps(0);
    stepperY.setCurrentPositionInSteps(0);
    stepperX.setAccelerationInStepsPerSecondPerSecond(ACCELERATION);
    stepperY.setAccelerationInStepsPerSecondPerSecond(ACCELERATION);
}

// Move both axes to the home position using normal acceleration
void goHome() {
    stepperX.setTargetPositionInSteps(0);
    stepperY.setTargetPositionInSteps(0);
}

// ---------------------------------------------------------------------------
// Arduino setup / loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW); // enable drivers

    pinMode(HOME_SWITCH_X, INPUT_PULLUP);
    pinMode(HOME_SWITCH_Y, INPUT_PULLUP);

    stepperX.connectToPins(STEP_PIN_X, DIR_PIN_X);
    stepperY.connectToPins(STEP_PIN_Y, DIR_PIN_Y);

    stepperX.setAccelerationInStepsPerSecondPerSecond(ACCELERATION);
    stepperY.setAccelerationInStepsPerSecondPerSecond(ACCELERATION);
    stepperX.setSpeedInStepsPerSecond(TOP_SPEED);
    stepperY.setSpeedInStepsPerSecond(TOP_SPEED);

    // Perform homing on startup at zero acceleration
    homeStartup();

    // Inform the host that we're ready
    Serial.println("NEXT");
}

String line;
bool lastMoving = false;

void loop() {
    // keep steppers running toward their targets
    stepperX.processMovement();
    stepperY.processMovement();

    // accumulate serial input line-by-line
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            line.trim();
            if (line.equalsIgnoreCase("Home")) {
                goHome();
            } else {
                float xcm, ycm;
                if (sscanf(line.c_str(), "X:%f,Y:%f", &xcm, &ycm) == 2) {
                    long xSteps = (long)(xcm * STEPS_PER_CM);
                    long ySteps = (long)(ycm * STEPS_PER_CM);
                    stepperX.setTargetPositionInSteps(xSteps);
                    stepperY.setTargetPositionInSteps(ySteps);
                }
            }
            line = "";
        } else {
            line += c;
        }
    }

    // Signal Python when movement is complete
    bool moving = !(stepperX.motionComplete() && stepperY.motionComplete());
    if (lastMoving && !moving) {
        Serial.println("NEXT");
    }
    lastMoving = moving;
}

