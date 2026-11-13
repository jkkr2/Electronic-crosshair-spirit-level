#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <SensorFusion.h> // Include aster94 Sensor Fusion library

// --- Pin Definitions ---
// BMI160
#define I2C_SDA      21
#define I2C_SCL      22
#define BMI160_ADDR  0x68

// 1.28" TFT
#define TFT_CS       5
#define TFT_DC       17
#define TFT_RST      16
//      SDA (MOSI / Data) GPIO 23
//      SCL (SCK / Clock) GPIO 18

#define TARE_BUTTON_PIN 4 // Safe non-strapping GPIO connected to a button leading to GND

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
SF fusion;

float rollOffset = 0.0, pitchOffset = 0.0;
const int centerX = 120;
const int centerY = 120;
const int maxRadius = 90; 

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Configure Tare Button with internal pull-up (Pressed = LOW, Released = HIGH)
  pinMode(TARE_BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  initBMI160();

  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(GC9A01A_BLACK);
  drawLevelBackground();

  // Initial startup tare
  performTare();
}

void loop() {
  // Check if the tare button is pressed (transition to LOW)
  if (digitalRead(TARE_BUTTON_PIN) == LOW) {
    performTare();
    // Simple debounce delay
    delay(300); 
  }

  float ax, ay, az, gx, gy, gz;
  readBMI160(ax, ay, az, gx, gy, gz);

  float dt = fusion.deltatUpdate();
  fusion.MahonyUpdate(gx * DEG_TO_RAD, gy * DEG_TO_RAD, gz * DEG_TO_RAD, ax, ay, az, dt);

  float roll  = fusion.getRoll();
  float pitch = fusion.getPitch();

  float taredRoll  = roll - rollOffset;
  float taredPitch = pitch - pitchOffset;

  updateSpiritLevel(taredRoll, taredPitch);
}

// --- BMI160 Initialization ---
void initBMI160() {
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x7E); Wire.write(0xB6); Wire.endTransmission();
  delay(100);
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x7E); Wire.write(0x11); Wire.endTransmission();
  delay(50);
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x7E); Wire.write(0x15); Wire.endTransmission();
  delay(50);
}

// --- Read Raw Data ---
void readBMI160(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(0x0C); 
  Wire.endTransmission(false);
  Wire.requestFrom(BMI160_ADDR, 12);

  if (Wire.available() >= 12) {
    int16_t raw_gx = Wire.read() | (Wire.read() << 8);
    int16_t raw_gy = Wire.read() | (Wire.read() << 8);
    int16_t raw_gz = Wire.read() | (Wire.read() << 8);
    int16_t raw_ax = Wire.read() | (Wire.read() << 8);
    int16_t raw_ay = Wire.read() | (Wire.read() << 8);
    int16_t raw_az = Wire.read() | (Wire.read() << 8);

    ax = (float)raw_ax / 16384.0;
    ay = (float)raw_ay / 16384.0;
    az = (float)raw_az / 16384.0;
    gx = (float)raw_gx / 16.4;
    gy = (float)raw_gy / 16.4;
    gz = (float)raw_gz / 16.4;
  }
}

// --- Tare Function (Captures current orientation reference) ---
void performTare() {
  // Flash center target or give visual feedback on screen
  tft.fillCircle(centerX, centerY, 4, GC9A01A_WHITE);
  
  int samples = 30;
  for (int i = 0; i < samples; i++) {
    float ax, ay, az, gx, gy, gz;
    readBMI160(ax, ay, az, gx, gy, gz);
    float dt = fusion.deltatUpdate();
    fusion.MahonyUpdate(gx * DEG_TO_RAD, gy * DEG_TO_RAD, gz * DEG_TO_RAD, ax, ay, az, dt);
    delay(10);
  }
  
  rollOffset  = fusion.getRoll();
  pitchOffset = fusion.getPitch();
  
  delay(100);
}

// --- Draw UI Background ---
void drawLevelBackground() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.drawCircle(centerX, centerY, 100, GC9A01A_DARKGREY);
  tft.drawCircle(centerX, centerY, 99, GC9A01A_DARKGREY);
  tft.drawCircle(centerX, centerY, 30, GC9A01A_BLUE);
  tft.drawCircle(centerX, centerY, 60, GC9A01A_BLUE);
  tft.drawLine(centerX - 100, centerY, centerX + 100, centerY, GC9A01A_DARKGREY);
  tft.drawLine(centerX, centerY - 100, centerX, centerY + 100, GC9A01A_DARKGREY);
  tft.drawPixel(centerX, centerY, GC9A01A_WHITE);
}

// --- Dynamic Spirit Level Visualizer ---
void updateSpiritLevel(float r, float p) {
  static int prevX = centerX;
  static int prevY = centerY;

  float sensitivity = 6.0; 
  int posX = centerX + constrain((int)(r * sensitivity), -maxRadius, maxRadius);
  int posY = centerY + constrain((int)(p * sensitivity), -maxRadius, maxRadius);

  if (posX != prevX || posY != prevY) {
    tft.fillCircle(prevX, prevY, 6, GC9A01A_BLACK);
    
    tft.drawPixel(prevX, centerY, GC9A01A_DARKGREY);
    tft.drawPixel(centerX, prevY, GC9A01A_DARKGREY);
    if (abs(prevX - centerX) <= 30 && abs(prevY - centerY) <= 30) {
      tft.drawCircle(centerX, centerY, 30, GC9A01A_BLUE);
    } else if (abs(prevX - centerX) <= 60 && abs(prevY - centerY) <= 60) {
      tft.drawCircle(centerX, centerY, 60, GC9A01A_BLUE);
    }

    uint16_t bubbleColor = (abs(r) < 2.0 && abs(p) < 2.0) ? GC9A01A_GREEN : GC9A01A_RED;
    tft.fillCircle(posX, posY, 6, bubbleColor);
    tft.drawCircle(posX, posY, 6, GC9A01A_WHITE);

    prevX = posX;
    prevY = posY;
  }
}