// CoreS3 + StepMotor v1.1 : 2-axis Touch Jog Pad (M5.Ex_I2C fixed + compact UI + Serial logs)

#include <Arduino.h>
#include <Wire.h>        // link Wire/Wire1 symbols for M5GFX internals
#include <M5Unified.h>
#include <AccelStepper.h>
#include "driver/i2c.h"  // I2C_NUM_0 / I2C_NUM_1

// ---------- STEP/DIR pins ----------
#define X_STEP_PIN 18
#define X_DIR_PIN  17
#define Y_STEP_PIN 6
#define Y_DIR_PIN  7
// (Z later): #define Z_STEP_PIN 13; #define Z_DIR_PIN 0

// ---------- StepMotor v1.1 protocol ----------
static uint8_t  MOD_ADDR     = 0x27;
static constexpr uint8_t REG_LIMIT   = 0x00;   // R
static constexpr uint8_t REG_ENABLE  = 0x01;   // W: 0x00 enable, 0x10 disable (EN active-low inside)
static constexpr uint8_t REG_FAULT   = 0x02;   // R
static constexpr uint8_t REG_RESET   = 0x03;   // R/W (per-axis bits)
static constexpr uint8_t REG_VERSION = 0xFE;   // R
static constexpr uint8_t REG_ADDR    = 0xFF;   // R/W
static constexpr uint32_t I2C_HZ     = 100000; // start conservative; can bump to 400k

// Bits are per-axis: bit0=X, bit1=Y, bit2=Z (typical)
static constexpr uint8_t RESET_ALL   = 0x07;  // X|Y|Z reset asserted
static constexpr uint8_t RESET_NONE  = 0x00;  // release reset on all

// Common “enable” candidates across firmware variants
static const uint8_t kEnablePatterns[] = {
  0x00, // documented "enable all"
  0x07, // per-axis X|Y|Z
  0x0F, // sometimes “all on”
  0xFF  // catch-all (if 1 means ON)
};

// ---------- Mechanics ----------
float stepsPerMM_X = 80.0f;
float stepsPerMM_Y = 80.0f;

// ---------- Steppers ----------
AccelStepper sx(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
AccelStepper sy(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);

// ---------- UI model ----------
struct Btn { int x,y,w,h; const char* label; };
enum BtnID { BTN_X_NEG, BTN_X_POS, BTN_Y_NEG, BTN_Y_POS, BTN_STEP, BTN_FEED, BTN_EN, BTN_DIS, BTN_POS, BTN_COUNT };
Btn btns[BTN_COUNT];
float stepSizes[3] = {0.1f, 1.0f, 10.0f}; int stepIdx=1;
float feeds[3]     = {600.0f, 1200.0f, 3000.0f}; int feedIdx=1;

// ---------- Utils ----------
void   logActivity(const String& s) { Serial.println(s); }
long   mm2stepsX(float mm){ return lroundf(mm * stepsPerMM_X); }
long   mm2stepsY(float mm){ return lroundf(mm * stepsPerMM_Y); }
float  steps2mmX(long s)   { return s / stepsPerMM_X; }
float  steps2mmY(long s)   { return s / stepsPerMM_Y; }

// ---------- M5.Ex_I2C helpers ----------
uint8_t i2cRead1(uint8_t addr, uint8_t reg) {
  auto &bus = M5.Ex_I2C;
  if (!bus.start(addr, /*read?*/false, I2C_HZ)) return 0xFF;
  if (!bus.write(&reg, 1)) { bus.stop(); return 0xFF; }
  if (!bus.restart(addr, /*read?*/true, I2C_HZ)) { bus.stop(); return 0xFF; }
  uint8_t b = 0xFF;
  if (!bus.read(&b, 1, /*last_nack=*/true)) { bus.stop(); return 0xFF; }
  bus.stop();
  return b;
}
bool i2cWrite1(uint8_t addr, uint8_t reg, uint8_t val) {
  auto &bus = M5.Ex_I2C;
  if (!bus.start(addr, /*read?*/false, I2C_HZ)) return false;
  uint8_t buf[2] = { reg, val };
  bool ok = bus.write(buf, 2);
  bus.stop();
  return ok;
}

void scanAndProbe() {
  auto &bus = M5.Ex_I2C;
  Serial.println("I2C scan (M5.Ex_I2C):");
  uint8_t pick = 0;
  for (uint8_t a = 8; a < 0x78; ++a) {
    bool ok = (bus.start(a, false, I2C_HZ) && bus.stop());
    if (ok) {
      Serial.printf("  - device @ 0x%02X\n", a);
      if (a == 0x27) pick = a; else if (!pick) pick = a;
    }
    delay(2);
  }
  if (pick) MOD_ADDR = pick;
  Serial.printf("Using MOD=0x%02X\n", MOD_ADDR);

  uint8_t ver   = i2cRead1(MOD_ADDR, REG_VERSION);
  uint8_t addrR = i2cRead1(MOD_ADDR, REG_ADDR);
  uint8_t lim   = i2cRead1(MOD_ADDR, REG_LIMIT);
  uint8_t flt   = i2cRead1(MOD_ADDR, REG_FAULT);
  Serial.printf("Version=0x%02X  AddrReg=0x%02X  Limits=0x%02X  Fault=0x%02X\n", ver, addrR, lim, flt);
}

bool driversEnable(bool on){
  bool ok = i2cWrite1(MOD_ADDR, REG_ENABLE, on ? 0x00 : 0x10);
  Serial.println(ok ? (on ? "EN: OK" : "DIS: OK") : (on ? "EN: FAILED" : "DIS: FAILED"));
  return ok;
}

bool driversResetRelease() {
  bool ok1 = i2cWrite1(MOD_ADDR, REG_RESET, RESET_ALL);  delay(5);
  bool ok2 = i2cWrite1(MOD_ADDR, REG_RESET, RESET_NONE); delay(5);
  Serial.println((ok1 && ok2) ? "RESET: toggled" : "RESET: FAILED");
  return ok1 && ok2;
}

void dumpRegs(const char* tag = nullptr) {
  uint8_t lim = i2cRead1(MOD_ADDR, REG_LIMIT);
  uint8_t flt = i2cRead1(MOD_ADDR, REG_FAULT);
  uint8_t ver = i2cRead1(MOD_ADDR, REG_VERSION);
  if (tag) { Serial.print(tag); Serial.print(' '); }
  Serial.printf("Regs: VER=0x%02X LIM=0x%02X FAULT=0x%02X\n", ver, lim, flt);
}

// Try multiple reset/enable patterns and fire raw pulses to see any twitch
void brutalEnableAndTest() {
  Serial.println("=== BRUTE ENABLE/RESET CYCLE ===");
  for (int r=0; r<2; ++r) {
    uint8_t a = (r==0) ? RESET_ALL  : RESET_NONE;
    uint8_t b = (r==0) ? RESET_NONE : RESET_ALL;
    Serial.printf("RESET sequence: assert=0x%02X, release=0x%02X\n", a, b);
    i2cWrite1(MOD_ADDR, REG_RESET, a); delay(5);
    i2cWrite1(MOD_ADDR, REG_RESET, b); delay(5);
    dumpRegs("after RESET");

    for (uint8_t pat : kEnablePatterns) {
      bool ok = i2cWrite1(MOD_ADDR, REG_ENABLE, pat);
      Serial.printf("ENABLE write 0x%02X -> %s\n", pat, ok ? "OK" : "FAIL");
      delay(10);
      dumpRegs("after EN");

      Serial.println("RAW: X 300"); // raw pulses – no AccelStepper involved
      pinMode(X_DIR_PIN, OUTPUT); digitalWrite(X_DIR_PIN, HIGH);
      pinMode(X_STEP_PIN, OUTPUT); digitalWrite(X_STEP_PIN, LOW);
      for (int i=0;i<300;++i){ digitalWrite(X_STEP_PIN, HIGH); delayMicroseconds(600); digitalWrite(X_STEP_PIN, LOW); delayMicroseconds(600); }

      Serial.println("RAW: Y 300");
      pinMode(Y_DIR_PIN, OUTPUT); digitalWrite(Y_DIR_PIN, HIGH);
      pinMode(Y_STEP_PIN, OUTPUT); digitalWrite(Y_STEP_PIN, LOW);
      for (int i=0;i<300;++i){ digitalWrite(Y_STEP_PIN, HIGH); delayMicroseconds(600); digitalWrite(Y_STEP_PIN, LOW); delayMicroseconds(600); }

      Serial.println("---");
    }
  }
  Serial.println("=== END BRUTE CYCLE ===");
}

// ---------- Motion (blocking, like your working version) ----------
void moveXY(float dx, float dy, float feed){
  long startX = sx.currentPosition();
  long startY = sy.currentPosition();
  long tx = startX + mm2stepsX(dx);
  long ty = startY + mm2stepsY(dy);
  float L = sqrtf(dx*dx + dy*dy); if (L == 0) return;
  float v_path = feed / 60.0f;
  float vx = (dx / L) * v_path * stepsPerMM_X;
  float vy = (dy / L) * v_path * stepsPerMM_Y;
  sx.setAcceleration(0); sy.setAcceleration(0);
  sx.setMaxSpeed(fabs(vx)); sy.setMaxSpeed(fabs(vy));
  sx.setSpeed(vx);          sy.setSpeed(vy);
  sx.moveTo(tx);            sy.moveTo(ty);
  logActivity(String("MOVE: dx=")+dx+" dy="+dy+" F="+feed+" | steps ("+startX+","+startY+") -> ("+tx+","+ty+")");
  while (sx.distanceToGo() != 0 || sy.distanceToGo() != 0){
    if (sx.distanceToGo()!=0) sx.runSpeed();
    if (sy.distanceToGo()!=0) sy.runSpeed();
  }
  logActivity(String("DONE: X=")+steps2mmX(sx.currentPosition())+" Y="+steps2mmY(sy.currentPosition())+" mm");
}

// ---------- UI ----------
void drawButton(const Btn& b, const char* text) {
  auto& lcd = M5.Display;
  lcd.drawRoundRect(b.x, b.y, b.w, b.h, 8, TFT_WHITE);
  lcd.setTextDatum(middle_center);
  lcd.setTextSize(1);
  lcd.setCursor(b.x + b.w/2, b.y + b.h/2);
  lcd.print(text);
}
void drawStatus(int w) {
  auto& lcd = M5.Display;
  lcd.fillRect(0, 0, w, 24, TFT_BLACK);
  lcd.setTextDatum(middle_left);
  lcd.setTextSize(1);
  lcd.setCursor(8, 12);
  lcd.printf("Step: %.3g mm  F: %.0f", stepSizes[stepIdx], feeds[feedIdx]);
}
void drawUI(){
  auto& lcd = M5.Display;
  int W = lcd.width(), H = lcd.height();
  lcd.fillScreen(TFT_BLACK);
  int margin=8, topBar=24, cols=3, rows=3;
  int gridW=W-2*margin, gridH=H-topBar-2*margin;
  int cellW=gridW/cols, cellH=gridH/rows;
  btns[BTN_X_NEG] = { margin+cellW*0+4, topBar+margin+cellH*0+4, cellW-8, cellH-8, "X-" };
  btns[BTN_X_POS] = { margin+cellW*1+4, topBar+margin+cellH*0+4, cellW-8, cellH-8, "X+" };
  btns[BTN_STEP ] = { margin+cellW*2+4, topBar+margin+cellH*0+4, cellW-8, cellH-8, "Step" };
  btns[BTN_Y_NEG] = { margin+cellW*0+4, topBar+margin+cellH*1+4, cellW-8, cellH-8, "Y-" };
  btns[BTN_Y_POS] = { margin+cellW*1+4, topBar+margin+cellH*1+4, cellW-8, cellH-8, "Y+" };
  btns[BTN_FEED ] = { margin+cellW*2+4, topBar+margin+cellH*1+4, cellW-8, cellH-8, "Feed" };
  btns[BTN_EN   ] = { margin+cellW*0+4, topBar+margin+cellH*2+4, cellW-8, cellH-8, "EN" };
  btns[BTN_DIS  ] = { margin+cellW*1+4, topBar+margin+cellH*2+4, cellW-8, cellH-8, "DIS" };
  btns[BTN_POS  ] = { margin+cellW*2+4, topBar+margin+cellH*2+4, cellW-8, cellH-8, "POS" };
  lcd.setTextDatum(middle_center);
  lcd.setTextSize(1);
  lcd.setCursor(W/2, 8);
  lcd.print("CoreS3 2-Axis Jog");
  for (int i=0;i<BTN_COUNT;i++) drawButton(btns[i], btns[i].label);
  drawStatus(W);
}
int hitButton(int px, int py){
  for (int i=0;i<BTN_COUNT;i++){ auto &b = btns[i];
    if (px>=b.x && px<(b.x+b.w) && py>=b.y && py<(b.y+b.h)) return i; }
  return -1;
}

// ---------- Tests ----------
void testPulseX(){
  Serial.println("TEST: AccelStepper X 200 steps");
  driversEnable(true);
  sx.setAcceleration(0);
  sx.setMaxSpeed(2000);
  sx.moveTo(sx.currentPosition() + 200);
  sx.runSpeedToPosition(); // blocks until done
  Serial.printf("TEST done: pos=%ld\n", sx.currentPosition());
}

// ---------- Setup / Loop ----------
void setup(){
  Serial.begin(115200);
  delay(200);
  logActivity("Boot: starting UI and motors");

  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power  = true;    // power rails for touch/sensors
  M5.begin(cfg);

  // Start external I2C on CoreS3 default pins
  M5.Ex_I2C.begin(I2C_NUM_1, 12, 11);

  logActivity(String("Touch enabled: ") + (M5.Touch.isEnabled() ? "YES" : "NO"));

  scanAndProbe();
  driversEnable(true);
  driversResetRelease();
  dumpRegs("after RESET/EN");

  // Brute cycle: try variants and raw pulses
  brutalEnableAndTest();

  // Steppers baseline
  sx.setAcceleration(0); sy.setAcceleration(0);
  sx.setMaxSpeed(20000); sy.setMaxSpeed(20000);
  sx.setCurrentPosition(0); sy.setCurrentPosition(0);

  logActivity("Ready. Step="+String(stepSizes[stepIdx])+"mm, Feed="+String(feeds[feedIdx])+"mm/min");
  drawUI();

  dumpRegs("pre TEST");
  testPulseX();
}

void loop(){
  M5.update();
  auto& tp  = M5.Touch;
  auto& lcd = M5.Display;

  if (tp.isEnabled() && tp.getCount()){
    auto t = tp.getDetail(0);
    if (t.isPressed() || t.wasPressed()){
      int id = hitButton(t.x, t.y);
      if (id >= 0){
        logActivity(String("Touch: ")+btns[id].label+" at ("+t.x+","+t.y+")");
        switch (id){
          case BTN_X_NEG: moveXY(-stepSizes[stepIdx], 0,   feeds[feedIdx]); Serial.println("X-"); break;
          case BTN_X_POS: moveXY( stepSizes[stepIdx], 0,   feeds[feedIdx]); Serial.println("X+"); break;
          case BTN_Y_NEG: moveXY(0,  -stepSizes[stepIdx],  feeds[feedIdx]); Serial.println("Y-"); break;
          case BTN_Y_POS: moveXY(0,   stepSizes[stepIdx],  feeds[feedIdx]); Serial.println("Y+"); break;
          case BTN_STEP: stepIdx = (stepIdx+1)%3; logActivity(String("STEP -> ")+stepSizes[stepIdx]+" mm"); drawStatus(lcd.width()); break;
          case BTN_FEED: feedIdx = (feedIdx+1)%3; logActivity(String("FEED -> ")+feeds[feedIdx]+" mm/min"); drawStatus(lcd.width()); break;
          case BTN_EN:   driversEnable(true);  break;
          case BTN_DIS:  driversEnable(false); break;
          case BTN_POS: {
            float xm = steps2mmX(sx.currentPosition()), ym = steps2mmY(sy.currentPosition());
            logActivity(String("POS: X=")+xm+" Y="+ym+" mm");
            lcd.fillRect(6, 26, lcd.width()-12, 18, TFT_BLACK);
            lcd.setTextDatum(middle_left);
            lcd.setTextSize(1);
            lcd.setCursor(10, 35);
            lcd.printf("X=%.3f  Y=%.3f", xm, ym);
          } break;
        }
        delay(120);
      }
    }
  }
}
