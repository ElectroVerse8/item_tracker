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

ESP_FlexyStepper stepperX;
ESP_FlexyStepper stepperY;

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
volatile bool targetPending = false;
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
        targetPending = true;
        portEXIT_CRITICAL(&targetMux);
        request->send(200, "text/plain", "OK");
    } else {
        request->send(400, "text/plain", "Missing parameters");
    }
}

void stepperTask(void *pvParameters) {
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW); // enable drivers (active-low assumed)

    stepperX.connectToPins(STEP_PIN_X, DIR_PIN_X);
    stepperY.connectToPins(STEP_PIN_Y, DIR_PIN_Y);
    stepperX.setAccelerationInStepsPerSecondPerSecond(800);
    stepperY.setAccelerationInStepsPerSecondPerSecond(800);
    stepperX.setSpeedInStepsPerSecond(2000);
    stepperY.setSpeedInStepsPerSecond(2000);

    for (;;) {
        stepperX.processMovement();
        stepperY.processMovement();

        // Apply new target if available
        bool apply = false;
        TargetPosition tgt;
        portENTER_CRITICAL(&targetMux);
        if (targetPending) {
            tgt = pendingTarget;
            targetPending = false;
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
            targetPending = true;
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

