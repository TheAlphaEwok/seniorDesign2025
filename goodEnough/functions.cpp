#include "functions.h"

// ---------------- Internal FSM state ----------------

static MachineState gState = STATE_MAIN_MENU;

// encoder tracking for menus / jog
static long  gLastEncCount = 0;

// --------------- Internal helpers (file-local) ---------------

static void lcdPrintLine(uint8_t row, const char* msg) {
    lcd.setCursor(0, row);
    lcd.print("                    "); // clear the row (20 spaces)
    lcd.setCursor(0, row);
    lcd.print(msg);
}

// rising-edge detection on button (non-blocking)
// returns true once per press
static bool buttonPressedEdge() {
    static bool last = false;
    bool now = (digitalRead(BUTTON_PIN) == LOW); // active low
    bool edge = (!last && now);
    last = now;
    return edge;
}

static int updateMenuRow(int currentRow, int maxRows) {
    long count = myEnc.read() / 4;
    long delta = count - gLastEncCount;

    if (delta > 0 && currentRow < maxRows - 1)
        currentRow++;
    else if (delta < 0 && currentRow > 0)
        currentRow--;

    gLastEncCount = count;
    return currentRow;
}

// ---------------- Homing ----------------

void autoHome() {
    lcd.clear();
    lcdPrintLine(0, "Homing...");

    // Move Y to limit
    motorY.setSpeed(-500);
    while (digitalRead(LIMIT_Y) == LOW) {
        motorY.runSpeed();
    }

    // Move X to limit
    motorX1.setSpeed(1000);
    motorX2.setSpeed(1000);
    while (digitalRead(LIMIT_X) == LOW) {
        motorX1.runSpeed();
        motorX2.runSpeed();
    }

    motorY.setCurrentPosition(0);
    motorX1.setCurrentPosition(0);
    motorX2.setCurrentPosition(0);

    // Back off limits
    motorX1.move(-300);
    motorX2.move(-300);
    while (motorX1.distanceToGo() != 0 || motorX2.distanceToGo() != 0) {
        motorX1.run();
        motorX2.run();
    }

    motorY.move(250);
    while (motorY.distanceToGo() != 0) {
        motorY.run();
    }

    lcdPrintLine(0, "Homing complete");
    delay(500);
}

// ---------------- Main menu handler ----------------

static void handleMainMenu() {
    static bool initialized = false;
    static int  row = 0;

    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "1. Automatic Mode");
        lcdPrintLine(1, "2. Manual Mode");
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
            gState = STATE_AUTO_MENU;
        } else {
            gState = STATE_MANUAL_MENU;
        }
    }
}

// ---------------- Auto menu + run FSM ----------------

enum AutoState {
    AUTO_IDLE = 0,
    AUTO_MOVE_X,
    AUTO_WAIT_X,
    AUTO_MOVE_Y,
    AUTO_WAIT_Y,
    AUTO_DECISION_MENU
};

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
            gState = STATE_AUTO_RUN;
        } else {
            gState = STATE_MAIN_MENU;
        }
    }
}

static void handleAutoRun() {
    static AutoState autoState = AUTO_IDLE;
    static int xIndex = 0;
    static int yIndex = 0;
    static int menuRow = 0;      // for decision menu
    static long lastEnc = 0;     // for menu selection

    if (autoState == AUTO_IDLE) {
        motorX1.setSpeed(2000);
        motorX2.setSpeed(2000);
        motorY.setSpeed(1000);

        xIndex = 0;
        yIndex = 0;

        lcd.clear();
        lcdPrintLine(0, "Starting Auto Mode");

        autoState = AUTO_MOVE_X;
    }

    switch (autoState) {

    // ----------------------------
    // MOVE X (new column)
    // ----------------------------
    case AUTO_MOVE_X:
        if (xIndex >= AUTO_NUM_X) {
            lcd.clear();
            lcdPrintLine(0, "Auto Complete");
            delay(500);
            autoState = AUTO_IDLE;
            gState = STATE_MAIN_MENU;
            break;
        }

        motorX1.move(-500);
        motorX2.move(-500);
        autoState = AUTO_WAIT_X;
        break;


    case AUTO_WAIT_X:
        motorX1.run();
        motorX2.run();

        if (motorX1.distanceToGo() == 0 &&
            motorX2.distanceToGo() == 0) {

            yIndex = 0;
            autoState = AUTO_MOVE_Y;
        }
        break;


    // ----------------------------
    // MOVE Y (one row)
    // ----------------------------
    case AUTO_MOVE_Y: {

        if (yIndex >= AUTO_NUM_Y) {
            xIndex++;
            autoState = AUTO_MOVE_X;
            break;
        }

        lcd.clear();
        lcdPrintLine(0, "Moving to Position");
        lcdPrintLine(1, String("X=" + String(xIndex) + " Y=" + String(yIndex)).c_str());

        long yTarget = (long)((yIndex + 1) * Y_MOVE);
        motorY.moveTo(yTarget);

        autoState = AUTO_WAIT_Y;
        break;
    }


    case AUTO_WAIT_Y:
        motorY.run();
        if (motorY.distanceToGo() == 0) {

            // Lower probe
            lcd.clear();
            lcdPrintLine(0, "Lowering Probe...");
            servo.write(135);        // down angle (adjust)
            delay(150);

            // Initialize the decision menu
            lcd.clear();
            lcdPrintLine(0, "1. Continue");
            lcdPrintLine(1, "2. Back");
            lcdPrintLine(2, "3. Exit");
            lcd.setCursor(0, 0);
            lcd.blink();

            menuRow = 0;
            lastEnc = myEnc.read() / 4;

            autoState = AUTO_DECISION_MENU;
        }
        break;


    // ----------------------------------------
    // WAIT FOR USER DECISION (NEW MENU STATE)
    // ----------------------------------------
    case AUTO_DECISION_MENU: {

        long count = myEnc.read() / 4;
        long delta = count - lastEnc;

        if (delta > 0 && menuRow < 2) menuRow++;
        if (delta < 0 && menuRow > 0) menuRow--;

        lastEnc = count;

        lcd.setCursor(0, menuRow);  // move cursor

        if (buttonPressedEdge()) {
            lcd.noBlink();

            // -------------------------------
            // OPTION 1: Continue forward
            // -------------------------------
            if (menuRow == 0) {
                servo.write(90); // raise probe
                delay(150);

                yIndex++;
                autoState = AUTO_MOVE_Y;
            }

            // -------------------------------
            // OPTION 2: Back one position
            // -------------------------------
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

            // -------------------------------
            // OPTION 3: Exit to main menu
            // -------------------------------
            else if (menuRow == 2) {
                servo.write(90);
                delay(150);

                autoState = AUTO_IDLE;
                gState = STATE_MAIN_MENU;
            }
        }

        break;
    }

    default:
        autoState = AUTO_IDLE;
        break;
    }
}

// ---------------- Manual menu + jog FSM ----------------

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
        case 0: gState = STATE_JOG_X; break;
        case 1: gState = STATE_JOG_Y; break;
        case 2: gState = STATE_JOG_Z; break;
        case 3: gState = STATE_MAIN_MENU; break;
        }
    }
}

// X jog: both X motors together, encoder -> position
static void handleJogX() {
    static bool initialized = false;
    static long targetPos = 0;

    if (!initialized) {
        lcd.clear();
        lcdPrintLine(0, "Jog X (enc)");
        lcdPrintLine(1, "Button = Back");

        targetPos = motorX1.currentPosition();
        gLastEncCount = myEnc.read() / 4;
        initialized = true;
    }

    long count = myEnc.read() / 4;
    long delta = count - gLastEncCount;
    gLastEncCount = count;

    if (delta != 0) {
        targetPos += delta * JOG_STEP_X;
        motorX1.moveTo(targetPos);
        motorX2.moveTo(targetPos);
    }

    motorX1.run();
    motorX2.run();

    if (buttonPressedEdge()) {
        initialized = false;
        gState = STATE_MANUAL_MENU;
    }
}

// Y jog
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

// Z jog (servo or future Z-stepper) – placeholder
static void handleJogZ() {
    static bool initialized = false;
    static long lastCount = 0;
    static int angle = 90;   // start centered

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

    // each encoder tick moves the servo by 1 degree (tunable)
    if (delta != 0) {
        angle += delta; 
        angle = constrain(angle, 0, 180);
        servo.write(angle);
    }

    if (buttonPressedEdge()) {
        initialized = false;
        gState = STATE_MANUAL_MENU;
    }
}

// ---------------- FSM public API ----------------

void fsmInit() {
    gState = STATE_MAIN_MENU;
}

void fsmUpdate() {
    switch (gState) {
    case STATE_MAIN_MENU:
        handleMainMenu();
        break;

    case STATE_AUTO_MENU:
        autoHome();
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
