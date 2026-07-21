#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

/* * PROJECT: OpenCamera (ESP32-C3)
 * VERSION: Shutter Controller (Direct Button & Servo Mirror - No PCF/No Stepper)
 * Logic: Drives rack-and-pinion shutter via TB6612FNG, controls mirror servo
 * (40° down, 75° up), measures speed via IR sensor, and reads Speed UP/DOWN 
 * buttons directly connected to GPIO 4 and 5.
 */

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Servo mirrorServo;

// --- VERIFIED PIN MAPPING (DIRECT IO VERSION) ---
const int PWMA_PIN       = 2; 
const int AIN1           = 4;  
const int AIN2           = 3;  
const int STBY           = 0;  

const int SENSOR_PIN     = 20;   // IR Feedback
const int TRIGGER_BTN    = 5;   // Shutter trigger button (GPIO 1)
const int SPEED_UP_BTN   = 6;   // Direct Speed UP button (GPIO 4)
const int SPEED_DOWN_BTN = 7;   // Direct Speed DOWN button (GPIO 5)
const int SERVO_PIN      = 1;   // Mirror Servo (GPIO 7)

// --- SERVO SETTINGS ---
const int SERVO_DOWN  = 40;  // Mirror down position
const int SERVO_UP    = 75;  // Mirror up position

// --- SETTINGS ---
int MOTOR_SPEED            = 185;    
const float GATE_DISTANCE  = 16.0;   // Distance sensor beam spans (mm)
const float SLIT_WIDTH     = 2.0;    // Physical width of shutter slit (mm)
const int RAMP_UP_DELAY    = 5;      // Initial inertia kick (ms)

unsigned long lastTriggerPress = 0;
unsigned long lastUpPress = 0;
unsigned long lastDownPress = 0;
const int DEBOUNCE_MS      = 200;

bool movingForward         = true;
float lastShutterMs        = 0.0;
bool oledDetected          = false;

// Helper to log both to Serial and optionally to OLED status line
void logSystemState(String statusMsg) {
  Serial.print("SYS_STATE: ");
  Serial.println(statusMsg);
  updateDisplay(statusMsg);
}

void updateDisplay(String statusMsg) {
  if (!oledDetected) return; 
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("PWM: "); display.print(MOTOR_SPEED);
  display.setCursor(80, 0);
  display.print(movingForward ? "FWD" : "REV");
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  
  display.setTextSize(2);
  display.setCursor(0, 25);
  if (lastShutterMs > 0) {
    display.print(lastShutterMs, 2); display.print("ms");
  } else {
    display.print("READY");
  }

  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print(statusMsg);
  display.display();
}

void stopMotor() {
  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);
  analogWrite(PWMA_PIN, 0);   
}

void fireShutter(bool forward) {
  // Step 1: Flip Mirror UP
  logSystemState("MIRROR UP");
  mirrorServo.write(SERVO_UP);
  delay(250); // Allow physical time for the mirror to clear the path

  logSystemState("RUNNING");
  Serial.print("FIRE_LOG: Direction=");
  Serial.print(forward ? "FORWARD" : "REVERSE");
  Serial.print(" | PWM Target=");
  Serial.println(MOTOR_SPEED);
  
  // Set Direction
  digitalWrite(AIN1, forward ? HIGH : LOW);
  digitalWrite(AIN2, forward ? LOW : HIGH);
  
  // Pulse motor
  analogWrite(PWMA_PIN, MOTOR_SPEED);
  
  unsigned long moveStartedAt = millis();
  
  // Scale timeout dynamically: higher speed (PWM 255) needs less time to complete
  // than lower speed (PWM 0). Range is constrained between 25ms and 200ms.
  long calculatedTimeout = map(MOTOR_SPEED, 0, 255, 200, 25);
  unsigned long timeout = constrain(calculatedTimeout, 25, 200);

  // Wait for shutter slit to reach sensor (Beam Blocked -> Open)
  while(digitalRead(SENSOR_PIN) == HIGH) {
    if (millis() - moveStartedAt > timeout) { 
      stopMotor(); 
      logSystemState("T-OUT 1");
      Serial.print("ERROR: Timeout waiting for shutter slit to reach sensor (Timeout: ");
      Serial.print(timeout);
      Serial.println("ms).");
      // Ensure mirror returns down in case of safety shutdown
      mirrorServo.write(SERVO_DOWN);
      return; 
    }
  }
  
  unsigned long t1 = micros(); // Slit start
  
  // Wait for slit to finish passing (Beam Open -> Blocked)
  while(digitalRead(SENSOR_PIN) == LOW) {
    if (millis() - moveStartedAt > timeout) { 
      stopMotor(); 
      logSystemState("STUCK 1");
      Serial.print("ERROR: Shutter stuck over IR beam sensor (Timeout: ");
      Serial.print(timeout);
      Serial.println("ms).");
      // Ensure mirror returns down in case of safety shutdown
      mirrorServo.write(SERVO_DOWN);
      return; 
    }
  }
  
  unsigned long t2 = micros(); // Slit end
  
  stopMotor();

  // Calculation: Time = Slit_Width / Velocity
  // Velocity = Gate_Distance / (t2 - t1)
  float durSec = (t2 - t1) / 1000000.0;
  if (durSec > 0) {
    float vel = GATE_DISTANCE / durSec;
    lastShutterMs = (SLIT_WIDTH / vel) * 1000.0;
    
    // Primary Web App / GUI compatible telemetry payload
    Serial.print("SHUTTER:"); 
    Serial.println(lastShutterMs, 4);

    // Diagnostics details
    Serial.print("DIAGNOSTICS: Velocity=");
    Serial.print(vel, 2);
    Serial.print(" mm/s | Pulse Duration=");
    Serial.print(t2 - t1);
    Serial.println(" us");
  } else {
    Serial.println("ERROR: Invalid timing calculation (durSec <= 0).");
  }

  logSystemState("DONE");

  // Step 2: Return Mirror DOWN
  delay(100); // Tiny pause before returning mirror
  logSystemState("MIRROR DN");
  mirrorServo.write(SERVO_DOWN);
  delay(250); // Allow physical time for mirror to settle down

  logSystemState("IDLE");
}

void setup() {
  Serial.begin(115200);
  
  // Let USB CDC catch up on ESP32-C3
  unsigned long startWait = millis();
  while(!Serial && millis() - startWait < 2000);

  Serial.println("\n==============================================");
  Serial.println("   OPENCAMERA SIMPLIFIED - FIRMWARE V2       ");
  Serial.println("==============================================");

  Wire.begin(8, 9); // SDA=8, SCL=9

  // Init OLED
  if(display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledDetected = true;
    Serial.println("I2C_INIT: SSD1306 OLED Configured.");
  } else {
    Serial.println("I2C_INIT: OLED failed to initialize. Relying on Serial backup.");
  }

  // Setup Outputs
  pinMode(PWMA_PIN, OUTPUT);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(STBY, OUTPUT);
  
  // Setup Inputs
  pinMode(SENSOR_PIN, INPUT);
  pinMode(TRIGGER_BTN, INPUT_PULLUP);
  pinMode(SPEED_UP_BTN, INPUT_PULLUP);
  pinMode(SPEED_DOWN_BTN, INPUT_PULLUP);
  
  // Servo Initialization using ESP32-specific timer allocations
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  mirrorServo.setPeriodHertz(50); // Standard 50Hz servo refresh rate
  mirrorServo.attach(SERVO_PIN, 500, 2400); // Attach with standard min/max pulse widths
  mirrorServo.write(SERVO_DOWN); // Initialize with mirror in down position
  Serial.println("HARDWARE: ESP32 Servo initialized on Pin 7 (Set to 40 deg).");

  digitalWrite(STBY, HIGH); // Driver enabled
  Serial.println("HARDWARE: Direct Pin configuration complete. Standby line pulled HIGH.");

  logSystemState("IDLE");
}

void loop() {
  // 1. Physical Trigger Button
  if (digitalRead(TRIGGER_BTN) == LOW) {
    if (millis() - lastTriggerPress > DEBOUNCE_MS) {
      lastTriggerPress = millis();
      Serial.println("TRIG: Shutter button pressed.");
      fireShutter(movingForward);
      movingForward = !movingForward; // Auto-return logic
    }
  }

  // 2. Direct Speed UP Button
  if (digitalRead(SPEED_UP_BTN) == LOW) {
    if (millis() - lastUpPress > DEBOUNCE_MS) {
      lastUpPress = millis();
      int oldSpeed = MOTOR_SPEED;
      MOTOR_SPEED = min(MOTOR_SPEED + 5, 255);
      if (oldSpeed != MOTOR_SPEED) {
        Serial.print("CONFIG: Speed Incremented. Motor PWM: ");
        Serial.println(MOTOR_SPEED);
        logSystemState("ADJ SPD");
      }
    }
  }

  // 3. Direct Speed DOWN Button
  if (digitalRead(SPEED_DOWN_BTN) == LOW) {
    if (millis() - lastDownPress > DEBOUNCE_MS) {
      lastDownPress = millis();
      int oldSpeed = MOTOR_SPEED;
      MOTOR_SPEED = max(MOTOR_SPEED - 5, 0);
      if (oldSpeed != MOTOR_SPEED) {
        Serial.print("CONFIG: Speed Decremented. Motor PWM: ");
        Serial.println(MOTOR_SPEED);
        logSystemState("ADJ SPD");
      }
    }
  }
}