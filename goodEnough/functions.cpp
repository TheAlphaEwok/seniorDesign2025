#include "functions.h"

/*
  ==============================
  CNC Spot Welder Controller FSM
  ==============================

  High-level behavior:
  - A finite state machine (FSM) drives the UI and motion logic without blocking
    (except in autoHome() which currently uses blocking while-loops).
  - An encoder + pushbutton are used to navigate menus and jog axes.
  - Automatic mode:
      * homes X/Y
      * steps through an X-by-Y grid of weld/probe positions
      * at each position: lower probe, show a small decision menu (Continue / Back / Exit)
  - Manual mode:
      * jog X, Y via encoder (AccelStepper position control)
      * jog Z via servo (placeholder for a future Z stepper)

  Notes for maintainers:
  - AccelStepper: moveTo()/move() sets a target, run()/runSpeed() must be called frequently.
  - Encoder scaling: myEnc.read()/4 assumes your encoder library counts 4 per detent.
  - Button is ACTIVE-LOW: pressed when digitalRead(BUTTON_PIN) == LOW.
*/

// ---------------- Internal FSM state ----------------

// Current top-level machine state (menu / auto / manual / jog)
static MachineState gState = STATE_MAIN_MENU;

// Encoder count snapshot used for menu selection and jog delta calculation
static long gLastEncCount = 0;

// --------------- Internal helpers (file-local) ---------------

/*
  Print a message on a specific LCD row.
  Clears the row first by printing 20 spaces (assumes 20x4 LCD).
*/
static void lcdPrintLine(uint8_t row, const char* msg) {
    lcd.setCursor(0, row);
    lcd.print("                    "); // clear the row (20 spaces)
    lcd.setCursor(0, row);
    lcd.print(msg);
}

/*
  Rising-edge detection for the pushbutton (non-blocking).
  - Returns true exactly once per press.
  - The button is ACTIVE-LOW.
*/
static bool buttonPressedEdge() {
    static bool last = false;                    // last sampled button state
    bool now = (digitalRead(BUTTON_PIN) == LOW); // current state (pressed == LOW)
    bool edge = (!last && now);                  // rising edge: not-pressed -> pressed
    last = now;
    return edge;
}

/*
  Generic menu selector driven by encoder movement.
  - currentRow: current highlighted row index
  - maxRows: number of menu rows/options (0..maxRows-1)
  Returns updated row after applying encoder delta.
*/
static int updateMenuRow(int currentRow, int maxRows) {
    long count = myEnc.read() / 4;          // encoder absolute count (scaled)
    long delta = count - gLastEncCount;     // how much encoder moved since last update

    // Move menu cursor down/up based on encoder direction, clamped to valid rows
    if (delta > 0 && currentRow < maxRows - 1)
        currentRow++;
    else if (delta < 0 && currentRow > 0)
        currentRow--;

    gLastEncCount = count; // remember for next call
    return currentRow;
}

// ---------------- Homing ----------------

/*
  autoHome():
  Homes X and Y axes using limit switches, then backs off the switches.
  IMPORTANT:
  - This function is BLOCKING (while loops). During this time, the UI/FSM won't update.
  - Assumes LIMIT_X and LIMIT_Y are wired such that:
      * digitalRead(LIMIT_*) == LOW means "not triggered" (or triggered) depending on your wiring.
    Your while conditions must match the hardware logic.

  Flow:
  1) Run Y toward its limit until limit changes state.
  2) Run both X motors toward X limit until limit changes state.
  3) Zero current positions.
  4) Move off the switches by a fixed number of steps.
*/
void autoHome() {
    lcd.clear();
    lcdPrintLine(0, "Homing...");

    // Move Y toward its limit switch using constant speed mode
    motorY.setSpeed(-500);
    while (digitalRead(LIMIT_Y) == LOW) {
        motorY.runSpeed();
    }

    // Move X toward its limit switch (two motors move together)
    motorX1.setSpeed(1000);
    motorX2.setSpeed(1000);
    while (digitalRead(LIMIT_X) == LOW) {
        motorX1.runSpeed();
        motorX2.runSpeed();
    }

    // Define the limit position as "0" for each axis
    motorY.setCurrentPosition(0);
    motorX1.setCurrentPosition(0);
    motorX2.setCurrentPosition(0);

    // Back off X limit switch so you're not holding the switch mechanically
    motorX1.move(-300);
    motorX2.move(-300);
    while (motorX1.distanceToGo() != 0 || motorX2.distanceToGo() != 0) {
        motorX1.run();
        motorX2.run();
    }

    // Back off Y limit switch
    motorY.move(250);
    while (motorY.distanceToGo() != 0) {
        motorY.run();
    }

    lcdPrintLine(0, "Homing complete");
    delay(500);
}

// ---------------- Main menu handler ----------------

/*
  handleMainMenu():
  Displays and navigates the main menu:
    1) Automatic Mode
    2) Manual Mode

  Behavior:
  - Uses a static "initialized" to run LCD setup once per entry into this state.
  - Encoder selects row, button press selects the highlighted entry.
*/
static void handleMainMenu() {
    static bool initialized = false;
    static int  row = 0;

    // One-time entry setup for this state
    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "1. Automatic Mode");
        lcdPrintLine(1, "2. Manual Mode");
        lcd.setCursor(0, 0);
        lcd.blink();                      // blink cursor at active row
        row = 0;
        gLastEncCount = myEnc.read() / 4; // baseline encoder count for deltas
        initialized = true;
    }

    // Update selection row from encoder
    int newRow = updateMenuRow(row, 2);
    if (newRow != row) {
        row = newRow;
        lcd.setCursor(0, row);
    }

    // Select option on button press
    if (buttonPressedEdge()) {
        lcd.noBlink();
        initialized = false; // force re-init next time we come back here
        if (row == 0) {
            gState = STATE_AUTO_MENU;    // go to auto menu (and home first in fsmUpdate)
        } else {
            gState = STATE_MANUAL_MENU;  // go to manual menu
        }
    }
}

// ---------------- Auto menu + run FSM ----------------

/*
  AutoState:
  Sub-state machine used inside STATE_AUTO_RUN.
  This lets auto mode advance step-by-step without blocking.
*/
enum AutoState {
    AUTO_IDLE = 0,         // initial/reset state
    AUTO_MOVE_X,           // command next X move
    AUTO_WAIT_X,           // wait for X move to finish (run motors)
    AUTO_MOVE_Y,           // command next Y move
    AUTO_WAIT_Y,           // wait for Y move to finish (run motor)
    AUTO_DECISION_MENU     // at a position: lower probe + wait for user decision
};

/*
  handleAutoMenu():
  Small menu shown before auto run:
    1) Start
    2) Go Back
*/
static void handleAutoMenu() {
    static bool initialized = false;
    static int  row = 0;

    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "1. Start");
        lcdPrintLine(1, "2. Go Back");
        lcd.setCursor(0, 0);
        lcd.blink();
        row = 0;
        gLastEncCount = myEnc.read() / 4;
        initialized = true;
    }

    int newRow = updateMenuRow(row, 2);
    if (newRow != row) {
        row = newRow;
        lcd.setCursor(0, row);
    }

    if (buttonPressedEdge()) {
        lcd.noBlink();
        initialized = false;
        if (row == 0) {
            gState = STATE_AUTO_RUN;   // start the auto sub-FSM
        } else {
            gState = STATE_MAIN_MENU;  // return to main menu
        }
    }
}

/*
  handleAutoRun():
  Runs the automatic positioning sequence using the AutoState sub-FSM.

  Important details:
  - xIndex and yIndex represent which grid cell you are in.
  - X motion: currently always moves by -500 steps per column (relative).
  - Y motion: uses moveTo() with a target derived from yIndex and Y_MOVE.
  - At each (xIndex, yIndex) position:
      1) move there
      2) lower probe (servo)
      3) show decision menu:
          - Continue: raise probe, go to next Y
          - Back: raise probe, go to previous position
          - Exit: raise probe, return to main menu

  NOTE: There are still short delay() calls around servo moves.
*/
static void handleAutoRun() {
    static AutoState autoState = AUTO_IDLE;
    static int xIndex = 0;
    static int yIndex = 0;

    // Decision menu tracking inside AUTO_DECISION_MENU
    static int  menuRow = 0;  // 0=Continue, 1=Back, 2=Exit
    static long lastEnc = 0;  // encoder baseline for decision menu

    // Entry/reset for automatic run
    if (autoState == AUTO_IDLE) {
        // Set speed limits for runSpeed/run() behavior (AccelStepper)
        motorX1.setSpeed(2000);
        motorX2.setSpeed(2000);
        motorY.setSpeed(1000);

        // Start at the first cell in the grid
        xIndex = 0;
        yIndex = 0;

        lcd.clear();
        lcdPrintLine(0, "Starting Auto Mode");

        autoState = AUTO_MOVE_X;
    }

    switch (autoState) {

    // ----------------------------
    // MOVE X (start of a new column)
    // ----------------------------
    case AUTO_MOVE_X:
        // Done when we've processed all X columns
        if (xIndex >= AUTO_NUM_X) {
            lcd.clear();
            lcdPrintLine(0, "Auto Complete");
            delay(500);
            autoState = AUTO_IDLE;
            gState = STATE_MAIN_MENU;
            break;
        }

        // Command a relative X move (both motors together)
        motorX1.move(-500);
        motorX2.move(-500);
        autoState = AUTO_WAIT_X;
        break;

    // Wait until both X motors reach their target
    case AUTO_WAIT_X:
        motorX1.run();
        motorX2.run();

        if (motorX1.distanceToGo() == 0 &&
            motorX2.distanceToGo() == 0) {

            // After reaching new X column, start Y at the first row
            yIndex = 0;
            autoState = AUTO_MOVE_Y;
        }
        break;

    // ----------------------------
    // MOVE Y (one row in current column)
    // ----------------------------
    case AUTO_MOVE_Y: {
        // If finished all Y rows in this column, advance to next X column
        if (yIndex >= AUTO_NUM_Y) {
            xIndex++;
            autoState = AUTO_MOVE_X;
            break;
        }

        // UI status
        lcd.clear();
        lcdPrintLine(0, "Moving to Position");
        lcdPrintLine(1, String("X=" + String(xIndex) + " Y=" + String(yIndex)).c_str());

        // Compute next Y target (absolute). (yIndex+1) means first move goes to 1*Y_MOVE.
        long yTarget = (long)((yIndex + 1) * Y_MOVE);
        motorY.moveTo(yTarget);

        autoState = AUTO_WAIT_Y;
        break;
    }

    // Wait until Y is at target, then lower probe and show decision menu
    case AUTO_WAIT_Y:
        motorY.run();
        if (motorY.distanceToGo() == 0) {

            // Lower probe (servo down)
            lcd.clear();
            lcdPrintLine(0, "Lowering Probe...");
            servo.write(135); // down angle (adjust for your linkage)
            delay(150);

            // Show decision menu (3 options)
            lcd.clear();
            lcdPrintLine(0, "1. Continue");
            lcdPrintLine(1, "2. Back");
            lcdPrintLine(2, "3. Exit");
            lcd.setCursor(0, 0);
            lcd.blink();

            // Initialize decision menu state
            menuRow = 0;
            lastEnc = myEnc.read() / 4;

            autoState = AUTO_DECISION_MENU;
        }
        break;

    // ----------------------------------------
    // WAIT FOR USER DECISION AT CURRENT POSITION
    // ----------------------------------------
    case AUTO_DECISION_MENU: {
        // Encoder-driven selection (0..2)
        long count = myEnc.read() / 4;
        long delta = count - lastEnc;

        if (delta > 0 && menuRow < 2) menuRow++;
        if (delta < 0 && menuRow > 0) menuRow--;

        lastEnc = count;

        // Move cursor to selected option
        lcd.setCursor(0, menuRow);

        // Execute option on button press
        if (buttonPressedEdge()) {
            lcd.noBlink();

            // OPTION 1: Continue forward to next Y position
            if (menuRow == 0) {
                servo.write(90); // raise probe
                delay(150);

                yIndex++;
                autoState = AUTO_MOVE_Y;
            }

            // OPTION 2: Go back one position (previous Y; or previous X column last Y)
            else if (menuRow == 1) {
                servo.write(90);
                delay(150);

                if (yIndex > 0) {
                    yIndex--;
                } else if (xIndex > 0) {
                    xIndex--;
                    yIndex = AUTO_NUM_Y - 1;
                }
                autoState = AUTO_MOVE_Y;
            }

            // OPTION 3: Exit auto mode back to main menu
            else if (menuRow == 2) {
                servo.write(90);
                delay(150);

                autoState = AUTO_IDLE;
                gState = STATE_MAIN_MENU;
            }
        }

        break;
    }

    // Safety fallback: reset if state is invalid
    default:
        autoState = AUTO_IDLE;
        break;
    }
}

// ---------------- Manual menu + jog FSM ----------------

/*
  handleManualMenu():
  Manual menu:
    1) X-Axis jog
    2) Y-Axis jog
    3) Z-Axis jog (servo)
    4) Go Back
*/
static void handleManualMenu() {
    static bool initialized = false;
    static int  row = 0;

    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "1. X-Axis");
        lcdPrintLine(1, "2. Y-Axis");
        lcdPrintLine(2, "3. Z-Axis");
        lcdPrintLine(3, "4. Go Back");
        lcd.setCursor(0, 0);
        lcd.blink();
        row = 0;
        gLastEncCount = myEnc.read() / 4;
        initialized = true;
    }

    int newRow = updateMenuRow(row, 4);
    if (newRow != row) {
        row = newRow;
        lcd.setCursor(0, row);
    }

    if (buttonPressedEdge()) {
        lcd.noBlink();
        initialized = false;
        switch (row) {
        case 0: gState = STATE_JOG_X;     break;
        case 1: gState = STATE_JOG_Y;     break;
        case 2: gState = STATE_JOG_Z;     break;
        case 3: gState = STATE_MAIN_MENU;break;
        }
    }
}

/*
  handleJogX():
  Manual jog for X.
  - Encoder delta changes a target position in steps of JOG_STEP_X.
  - Both X motors are commanded to the same target position.
  - run() must be called continuously to advance motion.
*/
static void handleJogX() {
    static bool initialized = false;
    static long targetPos = 0;

    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "Jog X (enc)");
        lcdPrintLine(1, "Button = Back");

        targetPos = motorX1.currentPosition(); // start from current X position
        gLastEncCount = myEnc.read() / 4;
        initialized = true;
    }

    long count = myEnc.read() / 4;
    long delta = count - gLastEncCount;
    gLastEncCount = count;

    // Update commanded target when encoder moves
    if (delta != 0) {
        targetPos += delta * JOG_STEP_X;
        motorX1.moveTo(targetPos);
        motorX2.moveTo(targetPos);
    }

    // Advance motors toward target
    motorX1.run();
    motorX2.run();

    // Exit back to manual menu
    if (buttonPressedEdge()) {
        initialized = false;
        gState = STATE_MANUAL_MENU;
    }
}

/*
  handleJogY():
  Manual jog for Y.
  - Encoder delta changes a target position in steps of JOG_STEP_Y.
  - motorY moves to target using moveTo()/run().
*/
static void handleJogY() {
    static bool initialized = false;
    static long targetPos = 0;

    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "Jog Y (enc)");
        lcdPrintLine(1, "Button = Back");

        targetPos = motorY.currentPosition();
        gLastEncCount = myEnc.read() / 4;
        initialized = true;
    }

    long count = myEnc.read() / 4;
    long delta = count - gLastEncCount;
    gLastEncCount = count;

    if (delta != 0) {
        targetPos += delta * JOG_STEP_Y;
        motorY.moveTo(targetPos);
    }

    motorY.run();

    if (buttonPressedEdge()) {
        initialized = false;
        gState = STATE_MANUAL_MENU;
    }
}

/*
  handleJogZ():
  Manual jog for Z (servo).
  - This is a placeholder for a future Z stepper.
  - Encoder delta increments/decrements the servo angle by 1 degree per tick.
  - Constrained to [0, 180] degrees.
*/
static void handleJogZ() {
    static bool initialized = false;
    static long lastCount = 0;
    static int angle = 90; // neutral starting angle

    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "Jog Z (Servo)");
        lcdPrintLine(1, "Rotate encoder");
        lcdPrintLine(2, "Button = Back");

        lastCount = myEnc.read() / 4;
        initialized = true;
    }

    long count = myEnc.read() / 4;
    long delta = count - lastCount;
    lastCount = count;

    if (delta != 0) {
        angle += (int)delta;         // 1 degree per encoder tick
        angle = constrain(angle, 0, 180);
        servo.write(angle);
    }

    if (buttonPressedEdge()) {
        initialized = false;
        gState = STATE_MANUAL_MENU;
    }
}

// ---------------- FSM public API ----------------

/*
  fsmInit():
  Call once in setup() to start the FSM at the main menu.
*/
void fsmInit() {
    gState = STATE_MAIN_MENU;
}

/*
  fsmUpdate():
  Call repeatedly in loop().
  Dispatches to the correct handler based on the current top-level state.
*/
void fsmUpdate() {
    switch (gState) {
    case STATE_MAIN_MENU:
        handleMainMenu();
        break;

    case STATE_AUTO_MENU:
        autoHome();        // NOTE: blocking homing; consider making this non-blocking later
        handleAutoMenu();
        break;

    case STATE_AUTO_RUN:
        handleAutoRun();
        break;

    case STATE_MANUAL_MENU:
        handleManualMenu();
        break;

    case STATE_JOG_X:
        handleJogX();
        break;

    case STATE_JOG_Y:
        handleJogY();
        break;

    case STATE_JOG_Z:
        handleJogZ();
        break;

    default:
        gState = STATE_MAIN_MENU;
        break;
    }
}
