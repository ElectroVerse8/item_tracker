#include <Arduino.h>
#include <WiFi.h>
#include <ESP_FlexyStepper.h>
#include <M5Unified.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// WiFi credentials (replace with actual network details)
const char* WIFI_SSID = "Robotpishop";
const char* WIFI_PASSWORD = "6BD6B9F3";

// Stepper motor pin definitions
const int ENABLE_PIN = 4; // PA4 shared enable
const int STEP_PIN_X = 16; // X-axis STEP
const int DIR_PIN_X  = 17; // X-axis DIR
const int STEP_PIN_Y = 12; // Y-axis STEP
const int DIR_PIN_Y  = 13; // Y-axis DIR

// Home switch pins (active-low)
const int HOME_SWITCH_X = 25;
const int HOME_SWITCH_Y = 26;

ESP_FlexyStepper stepperX;
ESP_FlexyStepper stepperY;

// Motion configuration
const float ACCELERATION = 800.0f;  // steps/s^2
const float TOP_SPEED    = 2000.0f; // steps/s
const float HOMING_SPEED = 800.0f;  // steps/s during homing
const long  HOMING_MAX_DISTANCE = 50000; // max steps to search for switch

struct TargetPosition {
    long x;
    long y;
};

struct SystemStatus {
    long currentX;
    long currentY;
    bool moving;
    bool error;
};

// Shared variables protected by critical sections
volatile TargetPosition pendingTarget = {0, 0};
volatile bool movePending = false; // flag set by event handlers
portMUX_TYPE targetMux = portMUX_INITIALIZER_UNLOCKED;

SystemStatus status = {0, 0, false, false};
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;

AsyncWebServer server(80);

void handleMove(AsyncWebServerRequest *request) {
    if (request->hasParam("x") && request->hasParam("y")) {
        long x = request->getParam("x")->value().toInt();
        long y = request->getParam("y")->value().toInt();
        portENTER_CRITICAL(&targetMux);
        pendingTarget.x = x;
        pendingTarget.y = y;
        movePending = true;
        portEXIT_CRITICAL(&targetMux);
        request->send(200, "text/plain", "OK");
    } else {
        request->send(400, "text/plain", "Missing parameters");
    }
}

void handleHome(AsyncWebServerRequest *request) {
    portENTER_CRITICAL(&targetMux);
    pendingTarget.x = 0;
    pendingTarget.y = 0;
    movePending = true;
    portEXIT_CRITICAL(&targetMux);
    request->send(200, "text/plain", "OK");
}

void stepperTask(void *pvParameters) {
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW); // enable drivers (active-low assumed)

    stepperX.connectToPins(STEP_PIN_X, DIR_PIN_X);
    stepperY.connectToPins(STEP_PIN_Y, DIR_PIN_Y);
    pinMode(HOME_SWITCH_X, INPUT_PULLUP);
    pinMode(HOME_SWITCH_Y, INPUT_PULLUP);

    // Startup homing with zero acceleration
    stepperX.setAccelerationInStepsPerSecondPerSecond(0);
    stepperY.setAccelerationInStepsPerSecondPerSecond(0);
    stepperX.setSpeedInStepsPerSecond(HOMING_SPEED);
    stepperY.setSpeedInStepsPerSecond(HOMING_SPEED);
    stepperX.moveToHomeInSteps(-1, HOMING_SPEED, HOMING_MAX_DISTANCE, HOME_SWITCH_X);
    stepperY.moveToHomeInSteps(-1, HOMING_SPEED, HOMING_MAX_DISTANCE, HOME_SWITCH_Y);
    stepperX.setCurrentPositionInSteps(0);
    stepperY.setCurrentPositionInSteps(0);

    // Restore normal motion settings
    stepperX.setAccelerationInStepsPerSecondPerSecond(ACCELERATION);
    stepperY.setAccelerationInStepsPerSecondPerSecond(ACCELERATION);
    stepperX.setSpeedInStepsPerSecond(TOP_SPEED);
    stepperY.setSpeedInStepsPerSecond(TOP_SPEED);

    for (;;) {
        stepperX.processMovement();
        stepperY.processMovement();

        // Apply new target if available
        bool apply = false;
        TargetPosition tgt;
        portENTER_CRITICAL(&targetMux);
        if (movePending) {
            tgt = pendingTarget;
            movePending = false;
            apply = true;
        }
        portEXIT_CRITICAL(&targetMux);
        if (apply) {
            stepperX.setTargetPositionInSteps(tgt.x);
            stepperY.setTargetPositionInSteps(tgt.y);
        }

        // Update status
        portENTER_CRITICAL(&statusMux);
        status.currentX = stepperX.getCurrentPositionInSteps();
        status.currentY = stepperY.getCurrentPositionInSteps();
        status.moving = !(stepperX.motionComplete() && stepperY.motionComplete());
        portEXIT_CRITICAL(&statusMux);

        vTaskDelay(1);
    }
}

void uiTask(void *pvParameters) {
    M5.begin();
    M5.Display.setTextSize(2);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    server.on("/move", HTTP_GET, handleMove);
    server.on("/home", HTTP_GET, handleHome);
    server.begin();

    for (;;) {
        M5.update();

        if (M5.Touch.ispressed()) {
            auto p = M5.Touch.getPressPoint();
            long x = map(p.x, 0, M5.Display.width(), 0, 10000);
            long y = map(p.y, 0, M5.Display.height(), 0, 10000);
            portENTER_CRITICAL(&targetMux);
            pendingTarget.x = x;
            pendingTarget.y = y;
            movePending = true;
            portEXIT_CRITICAL(&targetMux);
        }

        // Display current status
        SystemStatus local;
        portENTER_CRITICAL(&statusMux);
        local = status;
        portEXIT_CRITICAL(&statusMux);

        M5.Display.fillScreen(BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.printf("X:%ld Y:%ld\n", local.currentX, local.currentY);
        M5.Display.printf("State: %s\n", local.moving ? "Moving" : "Idle");

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void setup() {
    xTaskCreatePinnedToCore(uiTask, "UI", 8192, NULL, 1, NULL, 0);     // Core 0
    xTaskCreatePinnedToCore(stepperTask, "Stepper", 8192, NULL, 2, NULL, 1); // Core 1
}

void loop() {
    // Nothing here; tasks handle everything
}

