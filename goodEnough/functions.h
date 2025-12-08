#pragma once

#include <Arduino.h>
#include <AccelStepper.h>
#include <Encoder.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ---------------- Pin / HW defs ----------------

#define ENABLE_PIN 8

#define MOTOR_X1_STEP_PIN 2
#define MOTOR_X1_DIR_PIN  5
#define MOTOR_X2_STEP_PIN 4
#define MOTOR_X2_DIR_PIN  7
#define MOTOR_Y_STEP_PIN  3
#define MOTOR_Y_DIR_PIN   6

#define SERVO_PIN 11

#define BUTTON_PIN 14
#define ENC_CW     15
#define ENC_CCW    16

#define LCD_ROWS    4
#define LCD_COLUMNS 20
#define I2C_ADDRESS 0x27

#define LIMIT_Y 10
#define LIMIT_X 9

#define ONE_TURN 3200

// Your scaling constants
#define Y_SCALE 0.489048
#define X_SCALE 2.5358
#define Y_MOVE  107.8
#define X_MOVE  539.1

// Jog step in motor steps per encoder detent
#define JOG_STEP_X 10
#define JOG_STEP_Y 10

// Auto grid size (you used 3 x 6 in the test)
#define AUTO_NUM_X 3   // normally 16
#define AUTO_NUM_Y 6   // normally 11

// ---------------- Global hardware ----------------

// Defined in main.ino
extern AccelStepper motorX1;
extern AccelStepper motorX2;
extern AccelStepper motorY;
extern Encoder      myEnc;
extern LiquidCrystal_I2C lcd;
extern Servo        servo;

// ---------------- FSM types ----------------

enum MachineState {
    STATE_MAIN_MENU = 0,
    STATE_AUTO_MENU,
    STATE_AUTO_RUN,
    STATE_MANUAL_MENU,
    STATE_JOG_X,
    STATE_JOG_Y,
    STATE_JOG_Z
};

// ---------------- Public API ----------------

/**
 * @brief Blocking homing sequence (called once at startup or when needed).
 * Uses LIMIT_X and LIMIT_Y to find 0,0, then backs off.
 */
void autoHome();

/**
 * @brief Initialize FSM state and first screen.
 */
void fsmInit();

/**
 * @brief One iteration of the FSM. Call from loop().
 */
void fsmUpdate();
