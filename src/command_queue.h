/**
 * @file command_queue.h
 * @author Eli Jelesko (ebj5hec@virginia.edu)
 * @brief Implements a First-In, First-Out (FIFO) data structure for commands
 * @version 0.1
 * @date 2022-10-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef COMMAND_QUEUE_H_
#define COMMAND_QUEUE_H_

#include <stdint.h>
#include <stdbool.h>

// Command queue defines
#define COMMAND_QUEUE_SIZE          (128) // Must be at least 1 and less than 65535

// Node type for the command queue
typedef struct command_t{
    void (*p_entry)(void);
    void (*p_action)(void);
    void (*p_exit)(void);
    void (*p_is_done)(void);
} command_t;

// Function definitions
void command_queue_init(void);
bool command_queue_push(command_t value);
bool command_queue_pop(command_t* p_value);
uint16_t command_queue_get_size(void);
bool command_queue_is_empty(void);

#endif /* COMMAND_QUEUE_H_ */
