// M5Stack CoreS3 example that displays the current location and
// provides a red "NEXT" button on the touch screen. The board also
// drives two steppers and requests the next point when motion
// completes or the button is pressed.

#include <M5Unified.h>
#include <AccelStepper.h>

// ---------------- Stepper configuration ----------------
const int STEP_PIN_X = 2;
const int DIR_PIN_X  = 4;
const int STEP_PIN_Y = 15;
const int DIR_PIN_Y  = 16;

float stepsPerCm = 200.0f;   // ratio factor (steps per centimetre)
float accel       = 200.0f;   // acceleration in steps/s^2
float maxSpeed    = 1000.0f;  // maximum speed in steps/s

AccelStepper stepperX(AccelStepper::DRIVER, STEP_PIN_X, DIR_PIN_X);
AccelStepper stepperY(AccelStepper::DRIVER, STEP_PIN_Y, DIR_PIN_Y);

long targetX = 0;
long targetY = 0;
bool moving   = false;
String currentLabel;

// Height of the on‑screen button in pixels.
const int BUTTON_HEIGHT = 60;

void drawScreen() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString(currentLabel, M5.Lcd.width() / 2,
                      (M5.Lcd.height() - BUTTON_HEIGHT) / 2);

    // Draw the red NEXT button at the bottom of the screen.
    M5.Lcd.fillRect(0, M5.Lcd.height() - BUTTON_HEIGHT, M5.Lcd.width(),
                    BUTTON_HEIGHT, RED);
    M5.Lcd.setTextColor(BLACK, RED);
    M5.Lcd.drawString("NEXT", M5.Lcd.width() / 2,
                      M5.Lcd.height() - BUTTON_HEIGHT / 2);
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    stepperX.setMaxSpeed(maxSpeed);
    stepperY.setMaxSpeed(maxSpeed);
    stepperX.setAcceleration(accel);
    stepperY.setAcceleration(accel);

    currentLabel = "";
    drawScreen();

    Serial.println("READY");
    Serial.println("NEXT");  // request first point
}

void parseCommand(const String &line) {
    // Expect lines in the form "L1-R2-C3,X:12.34,Y:56.78"
    int comma = line.indexOf(',');
    if (comma == -1) return;
    currentLabel = line.substring(0, comma);
    drawScreen();

    int xIndex = line.indexOf("X:", comma);
    int yIndex = line.indexOf("Y:", comma);
    if (xIndex != -1 && yIndex != -1) {
        float x = line.substring(xIndex + 2, yIndex - 1).toFloat();
        float y = line.substring(yIndex + 2).toFloat();
        targetX = (long)(x * stepsPerCm);
        targetY = (long)(y * stepsPerCm);
        stepperX.moveTo(targetX);
        stepperY.moveTo(targetY);
        moving = true;
    }
}

void loop() {
    M5.update();

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
        Serial.println("NEXT");  // request next coordinate
    }

    // Check for touches on the NEXT button.
    if (M5.Touch.ispressed()) {
        auto t = M5.Touch.getDetail();
        if (t.y > M5.Lcd.height() - BUTTON_HEIGHT) {
            Serial.println("NEXT");
            delay(300);  // crude debounce
        }
    }
}


