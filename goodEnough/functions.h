#pragma once

#include <AccelStepper.h>
#include <Encoder.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

#define ENABLE_PIN 8

#define MOTOR_X1_STEP_PIN 2
#define MOTOR_X1_DIR_PIN 5
#define MOTOR_X2_STEP_PIN 4
#define MOTOR_X2_DIR_PIN 7
#define MOTOR_Y_STEP_PIN 3
#define MOTOR_Y_DIR_PIN 6

#define BUTTON_PIN 14
#define ENC_CW 15
#define ENC_CCW 16

#define LCD_ROWS 4
#define LCD_COLUMNS 20
#define I2C_ADDRESS 0x27

#define LIMIT_Y 10
#define LIMIT_X 9

#define ONE_TURN 3200
#define Y_SCALE 0.489048
#define X_SCALE 2.5358
#define Y_MOVE 107.8
#define X_MOVE 539.1

#define DEBOUNCE_DELAY 200

/**
 * @brief Runs stepper motors until they reach the end stops to find (x=0, y=0) position
 * @param motorX1 First X-axis motor
 * @param motorX2 Second X-axis motor
 * @param motorY Y-axis motor
 * @param limitX X-axis end-stop
 * @param limitY Y-axis end-stop
 */
void autoHome(AccelStepper &motorX1, AccelStepper &motorX2, AccelStepper &motorY, int limitX, int limitY);

/**
 * @brief Prints text on LCD on specified row
 * @param lcd LCD object
 * @param row Row which text will print, index starts at 0
 * @param msg Message to be displayed
 */
void lcdPrintLine(LiquidCrystal_I2C &lcd, int row, const char* msg);

/**
 * @brief Moves cursor to specify location on the lcd
 * @param row Current row cursor is on
 * @param enc Encoder object
 * @param lastCount Previous encoder count
 * @param maxRows Amount of row on given menu
 * @return Row which cursor should be on
 */
int moveCursor(int row, Encoder &enc, long &lastCount, int maxRows);

/**
 * @brief Checks if button is pressed
 * @param buttonPin Pin button is connected to
 * @return True if button pressed, false otherwise
 */
bool isButtonPressed(int buttonPin);

/**
 * @brief Runs automatic operation mode
 * @param motorX1 First X-axis motor
 * @param motorX2 Second X-axis motor
 * @param motorY Y-axis motor
 * @param lcd LCD object
 * @param buttonPin Pin button is connected to
 */
void automaticMode(AccelStepper &motorX1, AccelStepper &motorX2, AccelStepper &motorY, LiquidCrystal_I2C &lcd, int buttonPin);

/**
 * @brief Runs manual operation mode
 * @param motorX1 First X-axis motor
 * @param motorX2 Second X-axis motor
 * @param motorY Y-axis motor
 * @param lcd LCD object
 * @param buttonPin Pin button is connected to
 */
void manualMode(AccelStepper &motorX1, AccelStepper &motorX2, AccelStepper &motorY, LiquidCrystal_I2C &lcd, int buttonPin);
