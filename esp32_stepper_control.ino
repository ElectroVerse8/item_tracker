#include <AccelStepper.h>

// Pin definitions for X and Y stepper drivers
const int STEP_PIN_X = 2;
const int DIR_PIN_X  = 4;
const int STEP_PIN_Y = 15;
const int DIR_PIN_Y  = 16;

// Number of step pulses needed to move one centimeter.
// Adjust this to calibrate distance vs pulses.
float stepsPerCm = 200.0f; // ratio factor

// Acceleration in steps/s^2. Can be tuned at runtime if desired.
float accel = 200.0f;

// Maximum speed (steps/s) for the steppers
float maxSpeed = 1000.0f;

// Create stepper driver instances
AccelStepper stepperX(AccelStepper::DRIVER, STEP_PIN_X, DIR_PIN_X);
AccelStepper stepperY(AccelStepper::DRIVER, STEP_PIN_Y, DIR_PIN_Y);

long targetX = 0;
long targetY = 0;
bool moving = false;

void setup() {
    Serial.begin(115200);

    stepperX.setMaxSpeed(maxSpeed);
    stepperY.setMaxSpeed(maxSpeed);
    stepperX.setAcceleration(accel);
    stepperY.setAcceleration(accel);

    Serial.println("READY");
    Serial.println("NEXT"); // request first point
}

void parseCommand(const String &line) {
    // Expect lines in the form "X:12.34,Y:56.78"
    int xIndex = line.indexOf('X');
    int commaIndex = line.indexOf(',');
    int yIndex = line.indexOf('Y');
    if (xIndex != -1 && commaIndex != -1 && yIndex != -1) {
        float x = line.substring(xIndex + 2, commaIndex).toFloat();
        float y = line.substring(yIndex + 2).toFloat();
        targetX = (long)(x * stepsPerCm);
        targetY = (long)(y * stepsPerCm);
        stepperX.moveTo(targetX);
        stepperY.moveTo(targetY);
        moving = true;
    }
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            parseCommand(line);
        }
    }

    stepperX.run();
    stepperY.run();

    if (moving && stepperX.distanceToGo() == 0 && stepperY.distanceToGo() == 0) {
        moving = false;
        Serial.println("NEXT"); // request next coordinate
    }
}

