#include <ti/devices/msp432e4/driverlib/driverlib.h>
#include "StepperMotors.h"
#include "PortPins.h"
#include "Defines.h"
#include "Gpio.h"

/*
 * Initialize the first stepper (the X-direction stepper)
 */
void initializeStepperXGPIO(void)
{
    // Enable pin is active low
    gpioSetAsOutput(STEPPER_X_ENABLE_PORT, STEPPER_X_ENABLE_PIN);

    // MS1 | MS2 | MS3
    //   0 |   0 |  0    <=> Full Step
    gpioSetAsOutput(STEPPER_X_MS1_PORT, STEPPER_X_MS1_PIN);
    gpioSetAsOutput(STEPPER_X_MS2_PORT, STEPPER_X_MS2_PIN);
    gpioSetAsOutput(STEPPER_X_MS3_PORT, STEPPER_X_MS3_PIN);

    // Set the direction: 1 <=> Clockwise; 0 <=> Clockwise
    gpioSetAsOutput(STEPPER_X_DIR_PORT, STEPPER_X_DIR_PIN);
    gpioSetOutputHigh(STEPPER_X_DIR_PORT, STEPPER_X_DIR_PIN);

    // STEP is toggled to perform the stepping
    gpioSetAsOutput(STEPPER_X_STEP_PORT, STEPPER_X_STEP_PIN);
}

void initializeStepperMotor(StepperMotorType *StepperMotor, uint8_t StepperMotorID)
{
    if (StepperMotorID == STEPPER_X_ID) {
        initializeStepperXGPIO();
        StepperMotor->MotorID            = StepperMotorID;
        StepperMotor->DirPort            = STEPPER_X_DIR_PORT;
        StepperMotor->DirPin             = STEPPER_X_DIR_PIN;
        StepperMotor->StepPort           = STEPPER_X_STEP_PORT;
        StepperMotor->StepPin            = STEPPER_X_STEP_PIN;
        StepperMotor->EnablePort         = STEPPER_X_ENABLE_PORT;
        StepperMotor->EnablePin          = STEPPER_X_ENABLE_PIN;
        StepperMotor->CurrentState       = Disabled;
        StepperMotor->CurrentXPosition   = ColA;
        StepperMotor->CurrentYPosition   = Row1;
        StepperMotor->DesiredXPosition   = ColA;
        StepperMotor->DesiredYPosition   = Row1;
        StepperMotor->HalfStepsToDesired = 0;
    }
}

/*
 * Toggle the direction of the stepper motor identified by StepperID
 */
void toggleDirection(StepperMotorType *StepperMotor)
{
    gpioSetOutputToggle(StepperMotor->DirPort, StepperMotor->DirPin);
}

/*
 * Change the direction of the stepper motor identified by StepperID to clockwise
 */
void setDirectionClockwise(StepperMotorType *StepperMotor)
{
    gpioSetOutputHigh(StepperMotor->DirPort, StepperMotor->DirPin);
}

/*
 * Change the direction of the stepper motor identified by StepperID to counterclockwise
 */
void setDirectionCounterclockwise(StepperMotorType *StepperMotor)
{
    gpioSetOutputLow(StepperMotor->DirPort, StepperMotor->DirPin);
}

/*
 * Perform a half step (i.e., toggle the step pin)
 */
void halfStep(StepperMotorType *StepperMotor)
{
    gpioSetOutputToggle(StepperMotor->StepPort, StepperMotor->StepPin);
}

/*
 * Method to tell the stepper motor how to get to its next position
 * TODO: Implement for second axis once the next motor is mounted on the frame
 */
void goToPosition(StepperMotorType *StepperMotor, ArmXPosition desiredXPosition, ArmYPosition desiredYPosition, uint8_t tempNumTiles, uint8_t forward)
{
    // Disable the motor
    gpioSetOutputHigh(StepperMotor->EnablePort, StepperMotor->EnablePin);
    StepperMotor->CurrentState = Disabled;

    // Determine number of steps to reach the desired position
    StepperMotor->DesiredXPosition = desiredXPosition;
    StepperMotor->DesiredYPosition = desiredYPosition;

    // Temporary portion
    if (forward) {
        setDirectionCounterclockwise(StepperMotor);
    } else {
        setDirectionClockwise(StepperMotor);
    }
    StepperMotor->HalfStepsToDesired = HALF_STEPS_TO_NEXT_ROW*tempNumTiles; // TODO: Hard-coded for now, will be replaced by specific values for each row/column

    // Enable the motor
    gpioSetOutputLow(StepperMotor->EnablePort, StepperMotor->EnablePin);
    StepperMotor->CurrentState = Enabled;
}