/**
 * @file electromagnet.c
 * @author Nick Cooney (npc4crc@virginia.edu)
 * @brief Provides functions to control an electromagnet
 * @version 0.1
 * @date 2022-10-09
 * 
 * @copyright Copyright (c) 2022
 */

#include "electromagnet.h"

// Private functions
void electromagnet_attract(void);
void electromagnet_repel(void);
void electromagnet_disengage(void);

/**
 * @brief Initialize the electromagnet
 */
void electromagnet_init(void)
{
    // Initialize both PWM pins; set IN1=ON, IN2=OFF
    pwm_init(0, 0);
}

/**
 * @brief Turn the electromagnet on with attraction
 */
void electromagnet_attract(void)
{
    // IN1=OFF, IN2=ON
    pwm_set_duty_pk4(0);
    pwm_set_duty_pk5(E_MAG_DUTY_CYCLE);
}

/**
 * @brief Turn the electromagnet on with repulsion
 */
void electromagnet_repel(void)
{
    // IN1=ON, IN2=OFF
    pwm_set_duty_pk4(E_MAG_DUTY_CYCLE);
    pwm_set_duty_pk5(0);
}

/**
 * @brief Turn the electromagnet off
 */
void electromagnet_disengage(void)
{
    // IN1=OFF, IN2=OFF
    pwm_set_duty_pk4(0);
    pwm_set_duty_pk5(0);
}

/* Command Functions */

/**
 * @brief Builds the electromagnet command
 *
 * @param desired_state One of {enabled, disabled}
 * @return Pointer to the command object
 */
electromagnet_command_t* electromagnet_build_command(peripheral_state_t desired_state)
{
    // The thing to return
    electromagnet_command_t* p_command = (electromagnet_command_t*) malloc(sizeof(electromagnet_command_t));

    // Functions
    p_command->command.p_entry   = &electromagnet_entry;
    p_command->command.p_action  = &utils_empty_function;
    p_command->command.p_exit    = &utils_empty_function;
    p_command->command.p_is_done = &electromagnet_is_done;

    // Data
    p_command->desired_state = desired_state;

    return p_command;
}

/**
 * @brief Enables or disables the electromagnet
 *
 * @param command Pointer to the electromagnet command
 */
void electromagnet_entry(command_t* command)
{
    electromagnet_command_t* p_command = (electromagnet_command_t*) command;

    // Enable or disable the electromagnet as desired
    if (p_command->desired_state == enabled)
    {
        electromagnet_attract();
    }
    else
    {
        electromagnet_disengage();
    }
}

/**
 * @brief Always returns true since no action is done
 *
 * @param command Pointer to the electromagnet command
 * @return True always
 */
bool electromagnet_is_done(command_t* command)
{
    return true;
}

/* End electromagnet.c */
