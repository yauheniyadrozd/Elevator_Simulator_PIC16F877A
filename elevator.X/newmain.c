/*
 * ============================================================
 *  ELEVATOR SIMULATOR -- PIC16F877A
 *  Environment: MPLAB X IDE + XC8 compiler
 *  Simulator: PICSimLab (Breadboard mode)
 * ============================================================
 *
 *  SIMPLIFICATION FOR PICSIMLAB:
 *  All buttons and outputs are connected to one port
 *  using LogicL and LogicO components from PICSimLab.
 *  No external pull-up resistors or decoders are needed.
 *
 *  CONNECTION DIAGRAM (PICSimLab Breadboard):
 *
 *  PORTB -- INPUTS (buttons, active LOW, internal pull-up):
 *    RB0 -- button: go to floor 1
 *    RB1 -- button: go to floor 2
 *    RB2 -- button: go to floor 3
 *    RB3 -- button: go to floor 4
 *    RB4 -- button: go to floor 5
 *    RB5 -- emergency STOP button
 *    RB6 -- button: OPEN door manually
 *    RB7 -- button: CLOSE door manually
 *
 *  PORTD -- OUTPUTS (7-segment display, floor digit):
 *    RD0..RD6 -- segments a,b,c,d,e,f,g (common cathode)
 *    RD7      -- dot (DP) -- blinks when the elevator is moving
 *
 *  PORTC -- OUTPUTS (status LEDs):
 *    RC0 -- LED: elevator IDLE
 *    RC1 -- LED: elevator GOING UP
 *    RC2 -- LED: elevator GOING DOWN
 *    RC3 -- LED: DOOR OPEN
 *    RC4 -- LED: ALARM / emergency stop
 *    RC5 -- BUZZER
 *    RC6 -- MOTOR direction A (up simulation)
 *    RC7 -- MOTOR direction B (down simulation)
 *
 *  PORTA -- OUTPUTS (floor LEDs -- show where the elevator is):
 *    RA0 -- floor 1 LED
 *    RA1 -- floor 2 LED
 *    RA2 -- floor 3 LED
 *    RA3 -- floor 4 LED
 *    RA4 -- floor 5 LED
 *    RA5 -- "waiting for request" LED (blinks in IDLE mode)
 *
 * ============================================================
 *  ADVANCED FUNCTIONS:
 *   - SCAN elevator algorithm with direction priority
 *   - Request queue with duplicate removal
 *   - Alarm mode (STOP button)
 *   - Manual door control
 *   - Ride counter (shown every 10 seconds)
 *   - Floor limit protection
 *   - Energy saving mode (LED blinks when there are no requests)
 *   - Sound signals: 1 signal = arrival, 3 signals = alarm
 *   - History of the last 5 floors (RAM log)
 * ============================================================
 */

/* Configuration bits for PIC16F877A with 4 MHz crystal */
#pragma config FOSC  = XT        /* 4 MHz crystal oscillator          */
#pragma config WDTE  = OFF       /* watchdog timer disabled           */
#pragma config PWRTE = ON        /* power-up timer enabled            */
#pragma config BOREN = ON        /* brown-out reset enabled           */
#pragma config LVP   = OFF       /* LVP programming disabled          */
#pragma config CPD   = OFF       /* EEPROM memory protection disabled */
#pragma config WRT   = OFF       /* write protection disabled         */
#pragma config CP    = OFF       /* code protection disabled          */

#include <xc.h>
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------
 *  System constants
 * ------------------------------------------------------- */
#define _XTAL_FREQ       4000000UL  /* oscillator frequency 4 MHz       */
#define FLOORS           5          /* number of supported floors       */
#define DOOR_TIME_MS     3000       /* door open time in ms             */
#define BUZZER_TIME_MS   150        /* buzzer signal time in ms         */
#define BLINK_TIME_MS    500        /* LED blinking period in ms        */
#define COUNTER_LIMIT    10         /* show counter every N seconds     */
#define HISTORY_SIZE     5          /* number of last floors to store   */
#define DEBOUNCE_MS      30         /* button debounce time in ms       */
#define MOTOR_SPEED_MS   3          /* motor step delay in ms           */

/* -------------------------------------------------------
 *  7-segment display codes (common cathode)
 *  Bit order: DP-g-f-e-d-c-b-a
 * ------------------------------------------------------- */
const uint8_t SEG7[6] = {
    0b00111111,   /* 0 */
    0b00000110,   /* 1 */
    0b01011011,   /* 2 */
    0b01001111,   /* 3 */
    0b01100110,   /* 4 */
    0b01101101    /* 5 */
};

/* Dash code "-" displayed in alarm mode */
#define SEG7_DASH   0b01000000

/* Letter "E" code for Error */
#define SEG7_ERROR  0b01111001

/* -------------------------------------------------------
 *  Elevator state definition
 * ------------------------------------------------------- */
typedef enum {
    STATE_IDLE,        /* elevator is idle, no requests              */
    STATE_GOING_UP,    /* elevator is going up                       */
    STATE_GOING_DOWN,  /* elevator is going down                     */
    STATE_DOOR_OPEN,   /* door open, waiting for closing             */
    STATE_ALARM,       /* alarm mode -- elevator stopped             */
    STATE_MANUAL_DOOR  /* manual door control                        */
} ElevatorState;

/* -------------------------------------------------------
 *  Direction definition
 * ------------------------------------------------------- */
typedef enum {
    DIR_NONE,  /* no direction, elevator stopped */
    DIR_UP,    /* up                             */
    DIR_DOWN   /* down                           */
} ElevatorDirection;

/* -------------------------------------------------------
 *  Structure representing an elevator request
 * ------------------------------------------------------- */
typedef struct {
    uint8_t floor;    /* floor number (1..5)       */
    bool active;      /* whether request is active  */
} Request;

/* -------------------------------------------------------
 *  Global state variables
 * ------------------------------------------------------- */
volatile ElevatorState     state        = STATE_IDLE;
volatile ElevatorDirection direction    = DIR_NONE;
volatile uint8_t currentFloor          = 1;    /* current floor (1..5)       */
volatile uint8_t targetFloor           = 0;    /* target floor               */
volatile Request queue[FLOORS];                /* request queue              */
volatile uint16_t doorTimer            = 0;    /* door time countdown        */
volatile uint16_t blinkTimer           = 0;    /* LED blinking countdown     */
volatile uint16_t secondTimer          = 0;    /* second countdown           */
volatile uint8_t secondsCounter        = 0;    /* seconds counter            */
volatile uint32_t rideCounter          = 0;    /* total number of rides      */
volatile bool blinkFlag                = false;/* blinking state             */
volatile bool alarmFlag                = false;/* alarm flag                 */
volatile bool doorFlag                 = false;/* door state change flag     */

/* Floor history -- circular buffer */
volatile uint8_t history[HISTORY_SIZE] = {0};
volatile uint8_t historyIndex          = 0;

/* Previous button state for debouncing */
volatile uint8_t previousPORTB   = 0xFF;
volatile uint8_t debounceState   = 0xFF;
volatile uint8_t debounceCounter = 0;

/* -------------------------------------------------------
 *  Function prototypes
 * ------------------------------------------------------- */
void initPorts(void);
void initTimer(void);
void resetQueue(void);
void addRequest(uint8_t floor);
void removeRequest(uint8_t floor);
uint8_t nextTarget(void);
bool hasRequest(void);
void motorUp(void);
void motorDown(void);
void motorStop(void);
void openDoor(void);
void closeDoor(void);
void buzzerSignal(uint8_t count);
void updateDisplay(uint8_t floor);
void updateLEDs(void);
void updateFloorLEDs(void);
void scanButtons(void);
uint8_t readButtons(void);
void addToHistory(uint8_t floor);
void showRideCounter(void);
void handleAlarm(void);
void handleManualDoor(uint8_t buttons);
void processStateMachine(void);
void delayMs(uint16_t ms);

/* -------------------------------------------------------
 *  I/O port initialization
 * ------------------------------------------------------- */
void initPorts(void) {
    /* PORTA: floor LED outputs (RA0..RA5) */
    TRISA = 0b11000000;   /* RA0..RA5 = output, RA6..RA7 = input */
    PORTA = 0x00;

    /* PORTB: button inputs with internal pull-up */
    TRISB = 0xFF;         /* whole port = input */
    OPTION_REGbits.nRBPU = 0; /* enable internal pull-up on PORTB */

    /* PORTC: status LED, buzzer, motor outputs */
    TRISC = 0x00;         /* whole port = output */
    PORTC = 0x00;

    /* PORTD: 7-segment display output */
    TRISD = 0x00;         /* whole port = output */
    PORTD = 0x00;

    /* PORTE: output, unused safety setting */
    TRISE = 0x00;
    PORTE = 0x00;

    /* Disable ADC converters -- all pins digital */
    ADCON1 = 0b00000110;
}

/* -------------------------------------------------------
 *  Timer0 initialization -- interrupt every about 1 ms
 *  At 4 MHz, prescaler 1:4: TMR0 overflows every about 1 ms
 * ------------------------------------------------------- */
void initTimer(void) {
    OPTION_REGbits.T0CS = 0;    /* source: internal oscillator */
    OPTION_REGbits.PSA  = 0;    /* prescaler assigned to Timer0 */
    OPTION_REGbits.PS2  = 0;
    OPTION_REGbits.PS1  = 0;
    OPTION_REGbits.PS0  = 1;    /* prescaler 1:4 */
    TMR0 = 6;                   /* start value -- calibration */
    INTCONbits.T0IE     = 1;    /* enable Timer0 interrupt */
    INTCONbits.GIE      = 1;    /* enable global interrupts */
    INTCONbits.PEIE     = 1;    /* enable peripheral interrupts */
}

/* -------------------------------------------------------
 *  Interrupt service routine (ISR)
 *  Called every about 1 ms by Timer0
 * ------------------------------------------------------- */
void __interrupt() interruptHandler(void) {
    if (INTCONbits.T0IF) {
        INTCONbits.T0IF = 0;    /* clear interrupt flag */
        TMR0 = 6;               /* reload timer */

        /* --- Door time countdown --- */
        if (state == STATE_DOOR_OPEN && doorTimer > 0) {
            doorTimer--;
            if (doorTimer == 0) {
                doorFlag = true;  /* signal to main loop */
            }
        }

        /* --- LED blinking countdown --- */
        if (blinkTimer > 0) {
            blinkTimer--;
        } else {
            blinkTimer  = BLINK_TIME_MS;
            blinkFlag ^= 1;  /* toggle blinking state */
        }

        /* --- Seconds countdown for ride counter --- */
        secondTimer++;
        if (secondTimer >= 1000) {
            secondTimer = 0;
            secondsCounter++;
        }

        /* --- Button debouncing --- */
        uint8_t current = PORTB;
        if (current != debounceState) {
            debounceCounter++;
            if (debounceCounter >= DEBOUNCE_MS) {
                debounceState    = current;
                debounceCounter = 0;
            }
        } else {
            debounceCounter = 0;
        }
    }
}

/* -------------------------------------------------------
 *  Reset the whole request queue
 * ------------------------------------------------------- */
void resetQueue(void) {
    for (uint8_t i = 0; i < FLOORS; i++) {
        queue[i].floor  = i + 1;
        queue[i].active = false;
    }
}

/* -------------------------------------------------------
 *  Add request to queue, ignoring duplicates and current floor
 * ------------------------------------------------------- */
void addRequest(uint8_t floor) {
    if (floor < 1 || floor > FLOORS) return;
    if (floor == currentFloor && state == STATE_DOOR_OPEN) return;
    queue[floor - 1].active = true;
}

/* -------------------------------------------------------
 *  Remove request from queue after serving a floor
 * ------------------------------------------------------- */
void removeRequest(uint8_t floor) {
    if (floor < 1 || floor > FLOORS) return;
    queue[floor - 1].active = false;
}

/* -------------------------------------------------------
 *  Check whether there is any request in the queue
 * ------------------------------------------------------- */
bool hasRequest(void) {
    for (uint8_t i = 0; i < FLOORS; i++)
        if (queue[i].active) return true;
    return false;
}

/* -------------------------------------------------------
 *  SCAN algorithm -- choose the next target floor
 *  Priority: requests in current direction first, then return
 * ------------------------------------------------------- */
uint8_t nextTarget(void) {
    /* Search in current direction, or upward if idle */
    if (direction == DIR_UP || direction == DIR_NONE) {
        for (uint8_t f = currentFloor + 1; f <= FLOORS; f++)
            if (queue[f - 1].active) return f;
        /* No upward requests -- search downward */
        for (int8_t f = (int8_t)currentFloor - 1; f >= 1; f--)
            if (queue[f - 1].active) return (uint8_t)f;
    } else {
        /* Direction down */
        for (int8_t f = (int8_t)currentFloor - 1; f >= 1; f--)
            if (queue[f - 1].active) return (uint8_t)f;
        /* No downward requests -- search upward */
        for (uint8_t f = currentFloor + 1; f <= FLOORS; f++)
            if (queue[f - 1].active) return f;
    }
    return 0;  /* no requests */
}

/* -------------------------------------------------------
 *  Motor control
 * ------------------------------------------------------- */
void motorUp(void) {
    PORTCbits.RC6 = 1;   /* direction A -- up */
    PORTCbits.RC7 = 0;
    delayMs(MOTOR_SPEED_MS);
}

void motorDown(void) {
    PORTCbits.RC6 = 0;
    PORTCbits.RC7 = 1;   /* direction B -- down */
    delayMs(MOTOR_SPEED_MS);
}

void motorStop(void) {
    PORTCbits.RC6 = 0;
    PORTCbits.RC7 = 0;   /* both pins = 0 -> stop */
}

/* -------------------------------------------------------
 *  Door control
 * ------------------------------------------------------- */
void openDoor(void) {
    PORTCbits.RC3 = 1;      /* door open LED ON */
    doorTimer     = DOOR_TIME_MS;
    doorFlag      = false;
    buzzerSignal(1);        /* 1 signal = arrival */
}

void closeDoor(void) {
    PORTCbits.RC3 = 0;      /* door open LED OFF */
}

/* -------------------------------------------------------
 *  Buzzer -- generate a selected number of sound signals
 * ------------------------------------------------------- */
void buzzerSignal(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        PORTCbits.RC5 = 1;
        delayMs(BUZZER_TIME_MS);
        PORTCbits.RC5 = 0;
        if (i < count - 1) delayMs(BUZZER_TIME_MS);
    }
}

/* -------------------------------------------------------
 *  Update 7-segment display
 *  Shows current floor number (1..5)
 *  DP dot blinks when elevator is moving
 * ------------------------------------------------------- */
void updateDisplay(uint8_t floor) {
    uint8_t code = (floor >= 1 && floor <= 5) ? SEG7[floor] : SEG7_ERROR;

    /* Add blinking dot when elevator is moving */
    if ((state == STATE_GOING_UP || state == STATE_GOING_DOWN) && blinkFlag)
        code |= 0b10000000;  /* enable DP bit */
    else
        code &= 0b01111111;  /* disable DP bit */

    PORTD = code;
}

/* -------------------------------------------------------
 *  Update status LEDs (PORTC RC0..RC4)
 * ------------------------------------------------------- */
void updateLEDs(void) {
    /* Clear all status LEDs */
    PORTC &= 0b11100000;   /* keep RC5 buzzer, RC6 and RC7 motor */

    switch (state) {
        case STATE_IDLE:
            /* IDLE LED blinks when there are no requests */
            if (blinkFlag) PORTCbits.RC0 = 1;
            break;

        case STATE_GOING_UP:
            PORTCbits.RC1 = 1;   /* green LED -- going up */
            break;

        case STATE_GOING_DOWN:
            PORTCbits.RC2 = 1;   /* red LED -- going down */
            break;

        case STATE_DOOR_OPEN:
        case STATE_MANUAL_DOOR:
            PORTCbits.RC3 = 1;   /* yellow LED -- door open */
            break;

        case STATE_ALARM:
            /* alarm LED blinks */
            if (blinkFlag) PORTCbits.RC4 = 1;
            break;
    }
}

/* -------------------------------------------------------
 *  Update floor LEDs (PORTA RA0..RA4)
 *  Lights the current floor LED
 * ------------------------------------------------------- */
void updateFloorLEDs(void) {
    PORTA = 0x00;   /* turn off all floor LEDs */

    /* Turn on current floor LED */
    if (currentFloor >= 1 && currentFloor <= FLOORS)
        PORTA |= (1 << (currentFloor - 1));

    /* RA5 blinks when elevator is waiting without requests */
    if (state == STATE_IDLE && !hasRequest() && blinkFlag)
        PORTAbits.RA5 = 1;
}

/* -------------------------------------------------------
 *  Button scanning with debouncing
 *  Returns pressed button mask (active LOW -> inverted)
 * ------------------------------------------------------- */
uint8_t readButtons(void) {
    /* debounceState is updated in ISR every 1 ms */
    return (~debounceState) & 0xFF;   /* invert: 1 = pressed */
}

/* -------------------------------------------------------
 *  Main button scanning and request registration function
 * ------------------------------------------------------- */
void scanButtons(void) {
    uint8_t buttons = readButtons();

    /* Detect rising edge, meaning a new press */
    uint8_t newPresses = buttons & (~previousPORTB);
    previousPORTB = buttons;

    if (!newPresses) return;   /* no new presses */

    /* RB0..RB4 -- floor buttons 1..5 */
    for (uint8_t i = 0; i < FLOORS; i++) {
        if (newPresses & (1 << i)) {
            if (state != STATE_ALARM) {
                addRequest(i + 1);
            }
        }
    }

    /* RB5 -- emergency STOP button */
    if (newPresses & (1 << 5)) {
        handleAlarm();
    }

    /* RB6 / RB7 -- manual door control */
    handleManualDoor(newPresses);
}

/* -------------------------------------------------------
 *  Handle STOP button / emergency alarm
 *  First press = alarm, second press = return to work
 * ------------------------------------------------------- */
void handleAlarm(void) {
    if (state != STATE_ALARM) {
        /* Enter alarm mode */
        motorStop();
        closeDoor();
        state     = STATE_ALARM;
        alarmFlag = true;
        buzzerSignal(3);            /* 3 signals = alarm */
        PORTD = SEG7_DASH;          /* display dash */
    } else {
        /* Exit alarm mode */
        state     = STATE_IDLE;
        direction = DIR_NONE;
        alarmFlag = false;
        resetQueue();               /* clear queue */
        buzzerSignal(1);            /* 1 signal = return to work */
    }
}

/* -------------------------------------------------------
 *  Manual door control (RB6 = open, RB7 = close)
 * ------------------------------------------------------- */
void handleManualDoor(uint8_t buttons) {
    /* RB6 -- open door manually, only when elevator is idle */
    if ((buttons & (1 << 6)) && state == STATE_IDLE) {
        openDoor();
        state = STATE_MANUAL_DOOR;
    }

    /* RB7 -- close door manually */
    if ((buttons & (1 << 7)) &&
        (state == STATE_DOOR_OPEN || state == STATE_MANUAL_DOOR)) {
        closeDoor();
        doorTimer = 0;
        state     = STATE_IDLE;
    }
}

/* -------------------------------------------------------
 *  Save floor to historical circular queue
 * ------------------------------------------------------- */
void addToHistory(uint8_t floor) {
    history[historyIndex] = floor;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

/* -------------------------------------------------------
 *  Show ride counter on the display
 *  Displayed for 2 seconds every COUNTER_LIMIT seconds
 * ------------------------------------------------------- */
void showRideCounter(void) {
    /* Show the units digit of the ride counter, mod 10 */
    uint8_t digit = (uint8_t)(rideCounter % 10);
    PORTD = SEG7[digit] | 0b10000000;  /* DP enabled = counter mode */
    delayMs(2000);                     /* show for 2 seconds */
}

/* -------------------------------------------------------
 *  Main elevator state machine
 * ------------------------------------------------------- */
void processStateMachine(void) {
    switch (state) {

        /* ------------------------------------------------
         *  IDLE STATE -- elevator is stopped and checks queue
         * ------------------------------------------------ */
        case STATE_IDLE: {
            motorStop();

            /* Check if there is any request */
            if (!hasRequest()) break;

            /* Choose next target floor */
            targetFloor = nextTarget();
            if (targetFloor == 0) break;

            if (targetFloor == currentFloor) {
                /* We are already on the correct floor */
                removeRequest(currentFloor);
                openDoor();
                state = STATE_DOOR_OPEN;
            } else if (targetFloor > currentFloor) {
                direction = DIR_UP;
                state     = STATE_GOING_UP;
                rideCounter++;   /* increase ride counter */
                addToHistory(targetFloor);
            } else {
                direction = DIR_DOWN;
                state     = STATE_GOING_DOWN;
                rideCounter++;
                addToHistory(targetFloor);
            }
            break;
        }

        /* ------------------------------------------------
         *  GOING UP
         * ------------------------------------------------ */
        case STATE_GOING_UP: {
            motorUp();

            /* Floor travel simulation:
             * After MOTOR_SPEED_MS * 200 steps = 1 floor higher */
            static uint16_t stepCounter = 0;
            stepCounter++;

            if (stepCounter >= 200) {
                stepCounter = 0;

                /* Protection -- do not go above the last floor */
                if (currentFloor < FLOORS) {
                    currentFloor++;
                } else {
                    /* Upper limit reached -- alarm */
                    handleAlarm();
                    break;
                }

                /* Check if target was reached */
                if (currentFloor == targetFloor) {
                    motorStop();
                    removeRequest(currentFloor);
                    openDoor();
                    state = STATE_DOOR_OPEN;
                }

                /* Check if there is a request on this floor on the way */
                else if (queue[currentFloor - 1].active) {
                    motorStop();
                    removeRequest(currentFloor);
                    openDoor();
                    state = STATE_DOOR_OPEN;
                    /* Update target after closing the door */
                    targetFloor = nextTarget();
                }
            }
            break;
        }

        /* ------------------------------------------------
         *  GOING DOWN
         * ------------------------------------------------ */
        case STATE_GOING_DOWN: {
            motorDown();

            static uint16_t stepCounter2 = 0;
            stepCounter2++;

            if (stepCounter2 >= 200) {
                stepCounter2 = 0;

                /* Protection -- do not go below the first floor */
                if (currentFloor > 1) {
                    currentFloor--;
                } else {
                    /* Lower limit reached -- alarm */
                    handleAlarm();
                    break;
                }

                /* Check if target was reached */
                if (currentFloor == targetFloor) {
                    motorStop();
                    removeRequest(currentFloor);
                    openDoor();
                    state = STATE_DOOR_OPEN;
                }

                /* Check requests on the way */
                else if (queue[currentFloor - 1].active) {
                    motorStop();
                    removeRequest(currentFloor);
                    openDoor();
                    state = STATE_DOOR_OPEN;
                    targetFloor = nextTarget();
                }
            }
            break;
        }

        /* ------------------------------------------------
         *  DOOR OPEN -- waiting for closing
         * ------------------------------------------------ */
        case STATE_DOOR_OPEN: {
            if (doorFlag) {
                doorFlag = false;
                closeDoor();
                direction = DIR_NONE;
                state     = STATE_IDLE;
            }
            break;
        }

        /* ------------------------------------------------
         *  ALARM MODE -- elevator locked
         * ------------------------------------------------ */
        case STATE_ALARM: {
            motorStop();
            closeDoor();
            /* Waiting for STOP button press to unlock */
            break;
        }

        /* ------------------------------------------------
         *  MANUAL DOOR -- handled in button scanning
         * ------------------------------------------------ */
        case STATE_MANUAL_DOOR: {
            break;
        }
    }
}

/* -------------------------------------------------------
 *  Software delay using built-in __delay_ms
 * ------------------------------------------------------- */
void delayMs(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++)
        __delay_ms(1);
}

/* -------------------------------------------------------
 *  MAIN FUNCTION
 * ------------------------------------------------------- */
void main(void) {
    /* Hardware initialization */
    initPorts();
    initTimer();
    resetQueue();

    /* Startup sequence -- blink all LEDs */
    PORTA = 0xFF;
    PORTC = 0xFF;
    PORTD = 0xFF;
    delayMs(300);
    PORTA = 0x00;
    PORTC = 0x00;
    PORTD = 0x00;
    delayMs(100);

    /* Set elevator on floor 1 */
    currentFloor = 1;
    targetFloor  = 1;
    direction    = DIR_NONE;
    state        = STATE_IDLE;

    /* Show start floor and open door */
    updateDisplay(currentFloor);
    updateFloorLEDs();
    openDoor();
    state = STATE_DOOR_OPEN;

    /* -------------------------------------------------------
     *  Infinite main loop
     * ------------------------------------------------------- */
    while (1) {

        /* 1. Scan buttons and add requests to queue */
        scanButtons();

        /* 2. Process state machine */
        processStateMachine();

        /* 3. Update displays and LEDs */
        updateDisplay(currentFloor);
        updateLEDs();
        updateFloorLEDs();

        /* 4. Every COUNTER_LIMIT seconds -- show ride counter */
        if (secondsCounter >= COUNTER_LIMIT && state == STATE_IDLE) {
            secondsCounter = 0;
            showRideCounter();
        }
    }
}
