#include "functions.h"

// Global hardware objects
AccelStepper motorX1(AccelStepper::DRIVER, MOTOR_X1_STEP_PIN, MOTOR_X1_DIR_PIN);
AccelStepper motorX2(AccelStepper::DRIVER, MOTOR_X2_STEP_PIN, MOTOR_X2_DIR_PIN);
AccelStepper motorY (AccelStepper::DRIVER, MOTOR_Y_STEP_PIN,  MOTOR_Y_DIR_PIN);
Encoder       myEnc(ENC_CCW, ENC_CW);
LiquidCrystal_I2C lcd(I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);
Servo         servo;

void setup() {
    lcd.init();
    lcd.backlight();

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);  // enable steppers

    motorX1.setAcceleration(500);
    motorX1.setMaxSpeed(8000);
    motorX2.setAcceleration(500);
    motorX2.setMaxSpeed(8000);
    motorY.setAcceleration(500);
    motorY.setMaxSpeed(10000);

    pinMode(LIMIT_X, INPUT_PULLUP);
    pinMode(LIMIT_Y, INPUT_PULLUP);

    servo.attach(11);
    servo.write(90);

    Serial.begin(115200);

    lcd.noCursor();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Push Button To Begin");

    // Wait for initial button press
    while (digitalRead(BUTTON_PIN) == HIGH) { /* idle */ }
    delay(200); // crude debounce

    // Home once at startup
    autoHome();

    // Initialize FSM (main menu, etc.)
    fsmInit();
}

void loop() {
    fsmUpdate();
}
