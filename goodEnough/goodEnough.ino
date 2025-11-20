#include "functions.h"

AccelStepper motorX1(1, MOTOR_X1_STEP_PIN, MOTOR_X1_DIR_PIN);
AccelStepper motorX2(1, MOTOR_X2_STEP_PIN, MOTOR_X2_DIR_PIN);
AccelStepper motorY(1, MOTOR_Y_STEP_PIN, MOTOR_Y_DIR_PIN);
Encoder myEnc(ENC_CCW, ENC_CW);
LiquidCrystal_I2C lcd(I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);
Servo servo;

unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 200;

long rawCount = 0;
long lastCount = 0;
bool mode = 1;
int row = 0;
int test = 0;
long Xposition[3] = {0, 0, 0};

void setup() {
  
  lcd.init();
  lcd.backlight(); 

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);

  motorX1.setAcceleration(500);
  motorX1.setMaxSpeed(8000);

  motorX2.setAcceleration(500);
  motorX2.setMaxSpeed(8000);

  motorY.setAcceleration(500);
  motorY.setMaxSpeed(10000);

  pinMode(LIMIT_X, INPUT_PULLUP);
  pinMode(LIMIT_Y, INPUT_PULLUP);

  //servo.attach(17);

  Serial.begin(115200);

  lcd.cursor_off();
  lcd.setCursor(0,0);
  lcd.print("Push Button To Begin");
  while (digitalRead(BUTTON_PIN) == HIGH);
  delay(200);
}

void loop() {

  /* 
  Auto homes the probes to the "0,0" position. 
  Runs the steppers until they reach the end stop in which it then moves off the end
  stop slightly to release the button.
  */
  motorY.setSpeed(-500);
  while(digitalRead(LIMIT_Y) == LOW){
    motorY.runSpeed();
  }
  motorX1.setSpeed(1000);
  motorX2.setSpeed(1000);
  while(digitalRead(LIMIT_X) == LOW){
    motorX1.runSpeed();
    motorX2.runSpeed();
  }
  motorY.setCurrentPosition(0);
  motorX1.setCurrentPosition(0);
  motorX2.setCurrentPosition(0);

  motorX1.setSpeed(-1000);
  motorX2.setSpeed(-1000);
  motorY.setSpeed(500);

  motorX1.move(-300);
  motorX2.move(-300);
  while(motorX1.distanceToGo() != 0 && motorX2.distanceToGo() != 0){
      motorX1.run();
      motorX2.run();
  }
  motorY.move(250);
  while(motorY.distanceToGo() != 0) {
    motorY.run();
  }

  //Start menu allows for selection between the two modes
  row = 0;
  lcd.clear();
  lcd.blink();
  lcd.setCursor(0, 0);
  lcd.print("1. Automatic Mode");
  lcd.setCursor(0, 1);
  lcd.print("2. Manual Mode");
  lcd.setCursor(0, 0);
  lastCount = 0;

  //Moves cursor between options until button is pushed
  while (digitalRead(BUTTON_PIN) == HIGH) {
    if (myEnc.read()/4 - lastCount > 0 && row == 0) {
      row++;
      lcd.setCursor(0, row);
    } else if (myEnc.read()/4 - lastCount < 0 && row == 1) {
      row--;
      lcd.setCursor(0, row);
    }
    lastCount = myEnc.read()/4;
  }
  lcd.clear();
  if (row == 0) {
    lcd.setCursor(0, 0);
    lcd.print("1. Start");
    lcd.setCursor(0, 1);
    lcd.print("2. Go Back");
    lcd.setCursor(0, 0);    
    row = 0;
    delay(200);
    while (digitalRead(BUTTON_PIN) == HIGH) {
      if (myEnc.read()/4 - lastCount > 0 && row < 1) {
        row++;
        lcd.setCursor(0, row);
      } else if (myEnc.read()/4 - lastCount < 0 && row > 0) {
        row--;
        lcd.setCursor(0, row);
      }
      lastCount = myEnc.read()/4;
    }
    while (digitalRead(BUTTON_PIN) == LOW);
    lcd.clear();
    delay(200);
    while (digitalRead(BUTTON_PIN) == HIGH && digitalRead(LIMIT_X) == LOW && digitalRead(LIMIT_Y) == LOW) {
      if (row == 0) {
        motorX1.setSpeed(2000);
        motorX2.setSpeed(2000);
        motorY.setSpeed(1000);
        for(int i = 0; i < 3; i++) { //i<16 normally
          motorX1.move(-500);
          motorX2.move(-500);
          while(motorX1.distanceToGo() != 0 && motorX2.distanceToGo() != 0) {
            motorX1.run();
            motorX2.run();
          }
          for(int i = 0; i < 6; i++) { //i<11 normally
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Moving To");
            lcd.setCursor(0, 1);
            lcd.print("Next Position");
            motorY.moveTo((i+1)*Y_MOVE);
            while(motorY.distanceToGo() != 0) {
              motorY.run();
            }
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Ready To Spot Weld");
            lcd.setCursor(0, 2);
            lcd.print("Press Button");
            lcd.setCursor(0, 3);
            lcd.print("To Continue");
            while(digitalRead(BUTTON_PIN) == HIGH);
            lcd.clear();
            delay(200);            
          }
        }
        return;  
      } else {
        delay(200);
        return;
      }
    }
    test = 0;
  } else if (row == 1) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("1. X-Axis");
    lcd.setCursor(0, 1);
    lcd.print("2. Y-Axis");
    lcd.setCursor(0, 2);
    lcd.print("3. Z-Axis");
    lcd.setCursor(0, 3);
    lcd.print("4. Go Back");
    lcd.setCursor(0, 0);
    row = 0;
    delay(200);
    while (digitalRead(BUTTON_PIN) == HIGH) {
      if (myEnc.read()/4 - lastCount > 0 && row < 3) {
        row++;
        lcd.setCursor(0, row);
      } else if (myEnc.read()/4 - lastCount < 0 && row > 0) {
        row--;
        lcd.setCursor(0, row);
      }
      lastCount = myEnc.read()/4;
    }  
    lcd.clear();
    delay(200);
    lcd.setCursor(0, 0);
    lcd.print("Press Button to Exit");
    while (digitalRead(BUTTON_PIN) == HIGH && digitalRead(LIMIT_X) == LOW && digitalRead(LIMIT_Y) == LOW) {
      if (row == 0) {

      } else if (row == 1) {

      } else if (row == 2) {

      } else {
        delay(200);
        return;
      }

//        motorA.setCurrentPosition(0);
//        motorA.moveTo(ONE_TURN);
//        motorA.runSpeedToPosition();
    }
    while(digitalRead(BUTTON_PIN) == LOW);
    delay(200); 
  } else if (row == 2) {
    return;
  }
  delay(200);
}


// THIS IS A TEST