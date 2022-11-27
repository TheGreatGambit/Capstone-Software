/**
 * @file gantry.c
 * @author Nick Cooney (npc4crc@virginia.edu), Eli Jelesko (ebj5hec@virginia.edu), Keenan Alchaar (ka5nt@virginia.edu)
 * @brief Code to unite all other modules
 * @version 1.0
 * @date 2022-11-21
 * 
 * @copyright Copyright (c) 2022
 */

#include "gantry.h"

// Private functions
void gantry_limit_stop(uint8_t limit_readings);
void gantry_home(void);
void gantry_reset(void);
void gantry_kill(void);
void gantry_clear_command(gantry_command_t* gantry_command);

// Flags
static bool gantry_homing    = false;
static bool robot_is_done    = false;
static bool comm_is_done     = false;
static bool human_is_done    = false;
static bool msp_illegal_move = false;
bool sys_fault               = false;

/**
 * @brief Initializes all modules
 */
void gantry_init(void)
{
    // System clock and timer initializations
    clock_sys_init();
    clock_timer0a_init();               // X
    clock_timer1a_init();               // Y
    clock_timer2a_init();               // Z
    clock_timer3a_init();               // Switches
    clock_timer4a_init();               // Gantry
    clock_timer5a_init();               // Delay
    clock_timer6a_init();               // Sensor network
    clock_timer7c_init();               // Comm delay
    clock_start_timer(GANTRY_TIMER);

    // System level initialization of all other modules
    command_queue_init();
#ifdef PERIPHERALS_ENABLED
   electromagnet_init();
   led_init();
   sensornetwork_init();
#endif /* PERIPHERALS_ENABLED */
    switch_init();
    stepper_init_motors();
    rpi_init();
    chessboard_init();

#ifdef THREE_PARTY_MODE
    uart_init(UART_CHANNEL_0);
#endif
}

/**
 * @brief Stops stepper motors based on the current limit switch readings
 *  TODO: Change functionality beyond kill
 * 
 * @param limit_readings A limit switch reading configured according to the switch vport
 */
void gantry_limit_stop(uint8_t limit_readings)
{
    if ((!gantry_homing) && (limit_readings & (LIMIT_X_MASK | LIMIT_Y_MASK | LIMIT_Z_MASK)))
    {
//        gantry_kill();
    }
}

/**
 * @brief Resets the entire system (motors, stored chess boards, UART, etc.)
 */
void gantry_reset(void)
{
    // Clear the command queue
    command_queue_clear();

    // Home the motors
    gantry_home();

    // Reset the chess board
    chessboard_reset_all();

    // Reset the rpi
    rpi_reset_uart();
#ifdef THREE_PARTY_MODE
    uart_reset(USER_CHANNEL);
#endif
#ifdef FINAL_IMPLEMENTATION_MODE
    // Start a new game
    char user_color = 'W';
    uint16_t switch_data = switch_get_reading();
    if (switch_data & ROCKER_COLOR)
    {
        user_color = 'B';
    }
    rpi_transmit_start(user_color);
#endif
}

/**
 * @brief Hard stops the gantry system. Kills (but does not home) motors, sets sys_fault flag
 */
void gantry_kill(void)
{
    // Disable all motors
    stepper_x_stop();
    stepper_y_stop();
    stepper_z_stop();

    // Set the system fault flag
    sys_fault = true;

    // Clear the command queue (just in case)
    command_queue_clear();
}

/* Command Functions */

/**
 * @brief Build a gantry_human command
 *
 * @returns Pointer to the dynamically-allocated command
 */
gantry_command_t* gantry_human_build_command(void)
{
    // The thing to return
    gantry_command_t* p_command = (gantry_command_t*) malloc(sizeof(gantry_command_t));

    // Functions
    p_command->command.p_entry   = &utils_empty_function;
    p_command->command.p_action  = &gantry_human_action;
    p_command->command.p_exit    = &gantry_human_exit;
    p_command->command.p_is_done = &gantry_human_is_done;

    // Data
    p_command->move.source_file = FILE_ERROR;
    p_command->move.source_rank = RANK_ERROR;
    p_command->move.dest_file   = FILE_ERROR;
    p_command->move.dest_rank   = RANK_ERROR;
    p_command->move.move_type   = IDLE;

    // Game Status
    p_command->game_status = ONGOING;

    // The move to be sent (not used for this type of command)
    p_command->move_to_send[0] = '?';
    p_command->move_to_send[1] = '?';
    p_command->move_to_send[2] = '?';
    p_command->move_to_send[3] = '?';
    p_command->move_to_send[4] = '?';

    return p_command;
}

/*
 * @brief Continuously determines the move the human has made. Ends
 *        when the human hits the "end turn" button.
 *
 * @param command The gantry command being run
 */
void gantry_human_action(command_t* command)
{
#ifdef FINAL_IMPLEMENTATION_MODE
    gantry_command_t* p_gantry_command = (gantry_command_t*) command;

    // Read the current board state, store it in current_board's board_presence
    uint64_t board_reading = sensor_get_reading();
    current_board.board_presence = board_reading;

    // Interpret the board state, store the interpreted move into move var
    if (!chessboard_get_move(&previous_board, &current_board, p_gantry_command->move_to_send))
    {
        // Definitely illegal move
        msp_illegal_move = true;
    }
    else
    {
        msp_illegal_move = false;
    }
#endif

#ifdef THREE_PARTY_MODE
    char first_byte = 0;
    char instr_op_len = 0;
    char move[5];
    char check_bytes[2];
    char pi_message[9];
    uint8_t instr;

    // First, read the first two bytes of the entire message from terminal
    if (uart_receive(USER_CHANNEL, &first_byte, 1))
    {
        if (first_byte == START_BYTE)
        {
            if (uart_receive(USER_CHANNEL, &instr_op_len, 1))
            {
                instr = instr_op_len >> 4;
                if (instr == HUMAN_MOVE_INSTR)
                {
                    if (uart_receive(USER_CHANNEL, move, 5))
                    {
                        pi_message[0] = START_BYTE;
                        pi_message[1] = HUMAN_MOVE_INSTR_AND_LEN;
                        pi_message[2] = move[0];
                        pi_message[3] = move[1];
                        pi_message[4] = move[2];
                        pi_message[5] = move[3];
                        pi_message[6] = move[4];
                        utils_fl16_data_to_cbytes((uint8_t *) pi_message, 7, check_bytes);
                        pi_message[7] = check_bytes[0];
                        pi_message[8] = check_bytes[1];

                        // Transmit the move to the RPi (9 bytes long)
                        rpi_transmit(pi_message, 9);

                        // We're done!
                        human_is_done = true;
                    }
                }
            }
        }
    }
    else
    {
        // Didn't get anything; move on
    }
#endif
}

/**
 * @brief Places a human or comm command on the queue depending on if the MSP
 *        could verify the human's move was not certainly illegal.
 *
 * @param command The gantry command being run
 */
void gantry_human_exit(command_t* command)
{
#ifdef FINAL_IMPLEMENTATION_MODE
    gantry_command_t* p_gantry_command = (gantry_command_t*) command;

    if (msp_illegal_move)
    {
        // Place the gantry_human command on the queue until a legal move is given
        command_queue_push((command_t*)gantry_human_build_command());
    }
    else
    {
        // Place the gantry_comm command on the queue to send the message
        command_queue_push((command_t*)gantry_comm_build_command(p_gantry_command->move_to_send));
        msg_ready_to_send = true;
    }
#endif
#ifdef THREE_PARTY_MODE
    // Place the gantry_comm command on the queue to send the message
    command_queue_push((command_t*)gantry_comm_build_command(p_gantry_command->move_to_send));
    msg_ready_to_send = true;

    // Reset flag
    human_is_done = false;
#endif
}

/**
 * @brief Moves to the next command once the END_TURN button has been pressed
 * 
 * @param command The gantry command being run
 * @return true If the END_TURN button has been pressed, false otherwise
 */
bool gantry_human_is_done(command_t* command)
{
#ifdef FINAL_IMPLEMENTATION_MODE
    uint8_t switch_data = switch_get_reading();
    return (switch_data & BUTTON_END_TURN);
#elif defined(THREE_PARTY_MODE)
    return human_is_done;
#else
    return true;
#endif
}

/**
 * @brief Build a gantry_comm command
 *
 * @param move The move, in UCI notation, to send to the RPi
 *
 * @returns Pointer to the dynamically-allocated command
 */
gantry_command_t* gantry_comm_build_command(char move[5])
{
    // The thing to return
    gantry_command_t* p_command = (gantry_command_t*) malloc(sizeof(gantry_command_t));

    // Functions
    p_command->command.p_entry   = &utils_empty_function;
    p_command->command.p_action  = &gantry_comm_action;
    p_command->command.p_exit    = &gantry_comm_exit;
    p_command->command.p_is_done = &gantry_comm_is_done;

    // Data
    p_command->move.source_file = FILE_ERROR;
    p_command->move.source_rank = RANK_ERROR;
    p_command->move.dest_file   = FILE_ERROR;
    p_command->move.dest_rank   = RANK_ERROR;
    p_command->move.move_type   = IDLE;

    // Game Status
    p_command->game_status = ONGOING;

    // The move to be sent
    p_command->move_to_send[0] = move[0];
    p_command->move_to_send[1] = move[1];
    p_command->move_to_send[2] = move[2];
    p_command->move_to_send[3] = move[3];
    p_command->move_to_send[4] = move[4];

    return p_command;
}

/*
 * @brief Sends the move passed in through the command, then uses a timer to
 *        wait and resend the move if no ACK is received from the RPi
 *
 * @param command The gantry command being run
 */
void gantry_comm_action(command_t* command)
{
    gantry_command_t* p_gantry_command = (gantry_command_t*) command;

    char ack_byte;

    if (msg_ready_to_send)
    {
        // Send the human move
        rpi_transmit_human_move(p_gantry_command->move_to_send);

        // Don't resend the message unless interrupt sets send_msg
        msg_ready_to_send = false;

        // Start the timer
        clock_start_timer(COMM_TIMER);
    }
    else
    {
        if (rpi_receive(&ack_byte, 1))
        {
            if (ack_byte == ACK_BYTE)
            {
                // ACK byte received!
                comm_is_done = true;

                // Stop the timer
                clock_stop_timer(COMM_TIMER);

                // Reset the value
                clock_reset_timer_value(COMM_TIMER);
            }
        }
    }
}

void gantry_comm_exit(command_t* command)
{
    // Verified comm, so push a robot command onto the queue
    command_queue_push((command_t*)gantry_robot_build_command());

    // Reset the flag
    comm_is_done = false;
}

bool gantry_comm_is_done(command_t* command)
{
    return comm_is_done;
}

/**
 * @brief Build a gantry_robot command
 *
 * @returns Pointer to the dynamically-allocated command
 */
gantry_command_t* gantry_robot_build_command(void)
{
    // The thing to return
    gantry_command_t* p_command = (gantry_command_t*)malloc(sizeof(gantry_command_t));

    // Functions
    p_command->command.p_entry   = &gantry_robot_entry;
    p_command->command.p_action  = &gantry_robot_action;
    p_command->command.p_exit    = &gantry_robot_exit;
    p_command->command.p_is_done = &gantry_robot_is_done;

    // Data
    p_command->move.source_file = FILE_ERROR;
    p_command->move.source_rank = RANK_ERROR;
    p_command->move.dest_file   = FILE_ERROR;
    p_command->move.dest_rank   = RANK_ERROR;
    p_command->move.move_type   = IDLE;

    // Game Status
    p_command->game_status = ONGOING;

    // The robot's move in uci notation
    p_command->robot_move_uci[0] = '?';
    p_command->robot_move_uci[1] = '?';
    p_command->robot_move_uci[2] = '?';
    p_command->robot_move_uci[3] = '?';
    p_command->robot_move_uci[4] = '?';

    // The move to be sent (not used for this type of command)
    p_command->move_to_send[0] = '?';
    p_command->move_to_send[1] = '?';
    p_command->move_to_send[2] = '?';
    p_command->move_to_send[3] = '?';
    p_command->move_to_send[4] = '?';

    return p_command;
}

/**
 * @brief Prepares the gantry for a move command. Nothing is done in this case.
 * 
 * @param command The gantry command being run
 */
void gantry_robot_entry(command_t* command)
{
    gantry_command_t* gantry_robot_move_cmd = (gantry_command_t*) command;

    // Reset everything
    robot_is_done = false;
    gantry_robot_move_cmd->move.source_file = FILE_ERROR;
    gantry_robot_move_cmd->move.source_rank = RANK_ERROR;
    gantry_robot_move_cmd->move.dest_file   = FILE_ERROR;
    gantry_robot_move_cmd->move.dest_rank   = RANK_ERROR;
    gantry_robot_move_cmd->move.move_type   = IDLE;
}

/**
 * @brief Attempts to read from the RPi until data has been received
 * 
 * @param command The gantry command being run
 */
void gantry_robot_action(command_t* command)
{
#ifdef USER_MODE
    gantry_command_t* p_gantry_command = (gantry_command_t*) command;
    // TODO: Waits, checksums, etc. as needed

    // Receive a move from the RPi and load it into the command struct for the exit() function

    /* RPi can send back 3 instructions: ROBOT_MOVE, ILLEGAL_MOVE,
     * or GAME_STATUS. Only the first two should be possible here.
     * GAME_STATUS will only be sent in response to a legal move. */

    /* Receive arrays */
    char move[5]; // NOTE: May not be used if ILLEGAL_MOVE is received
    if (rpi_receive(move, 5))
    {
        /*
         * Dev notes:
         * Order of operations: (if timeout at any point reset)
         * 1. Wait for start byte
         * 2. Once start sequence is received, read the order and assign to variables
         * 3. Calculate checksum
         * 4. If checksum passes, send an ack and return the move struct
         * 4b. If checksum fails, send a bad ack and goto step 1.
         */
        p_gantry_command->move.source_file = utils_byte_to_file(move[0]);
        p_gantry_command->move.source_rank = utils_byte_to_rank(move[1]);
        p_gantry_command->move.dest_file   = utils_byte_to_file(move[2]);
        p_gantry_command->move.dest_rank   = utils_byte_to_rank(move[3]);
        p_gantry_command->move.move_type   = utils_byte_to_move_type(move[4]);

        robot_is_done = true; // We've got the data we need
    }
#endif /* USER_MODE */

#if defined(FINAL_IMPLEMENTATION_MODE) || defined(THREE_PARTY_MODE)
    gantry_command_t* p_gantry_command = (gantry_command_t*) command;
    char first_byte = 0;
    char instr_op_len = 0;
    char move[5];
    char game_status = 0;
    char check_bytes[2];
    char message[8];
    uint8_t instr;
    uint8_t status_after_human;
    uint8_t status_after_robot;

    // First, read the first two bytes of the entire message
    if (rpi_receive(&first_byte, 1))
    {
        if (first_byte == START_BYTE)
        {
            if (rpi_receive(&instr_op_len, 1))
            {
                instr = instr_op_len >> 4;
                if (instr == ROBOT_MOVE_INSTR)
                {
                    if (rpi_receive(move, 5))
                    {
                        if (rpi_receive(&game_status, 1))
                        {
                            message[0] = first_byte;
                            message[1] = instr_op_len;
                            message[2] = move[0];
                            message[3] = move[1];
                            message[4] = move[2];
                            message[5] = move[3];
                            message[6] = move[4];
                            message[7] = game_status;
                            rpi_receive(check_bytes, 2);
                            if (utils_validate_transmission((uint8_t *) message, 8, check_bytes))
                            {
                                // Send ack
                                rpi_transmit_ack();
                                // Human's move wasn't illegal, so update previous_board to current_board
                                previous_board.board_presence = current_board.board_presence;
                                utils_copy_array(current_board.board_pieces, previous_board.board_pieces);
                                // Split up the game statuses
                                status_after_human = game_status >> 4;
                                status_after_robot = game_status & (~0xF0);
                                // Write the UCI move to update the board later
                                p_gantry_command->robot_move_uci[0] = move[0];
                                p_gantry_command->robot_move_uci[1] = move[1];
                                p_gantry_command->robot_move_uci[2] = move[2];
                                p_gantry_command->robot_move_uci[3] = move[3];
                                p_gantry_command->robot_move_uci[4] = move[4];
                                if (status_after_human == GAME_CHECKMATE)
                                {
                                    // If the human ended the game, don't let the robot move
                                    p_gantry_command->game_status = HUMAN_WIN;
                                    p_gantry_command->move.move_type = IDLE;
                                }
                                else if (status_after_robot == GAME_CHECKMATE)
                                {
                                    p_gantry_command->game_status = ROBOT_WIN;
                                    // Let the robot make its final move
                                    p_gantry_command->move.source_file = utils_byte_to_file(move[0]);
                                    p_gantry_command->move.source_rank = utils_byte_to_rank(move[1]);
                                    p_gantry_command->move.dest_file = utils_byte_to_file(move[2]);
                                    p_gantry_command->move.dest_rank = utils_byte_to_rank(move[3]);
                                    p_gantry_command->move.move_type = utils_byte_to_move_type(move[4]);
                                }
                                else if (status_after_human == GAME_STALEMATE)
                                {
                                    // If the human ended the game, don't let the robot move
                                    p_gantry_command->game_status = STALEMATE;
                                    p_gantry_command->move.move_type = IDLE;
                                }
                                else if (status_after_robot == GAME_STALEMATE)
                                {
                                    p_gantry_command->game_status = STALEMATE;
                                    // Let the robot make its final move
                                    p_gantry_command->move.source_file = utils_byte_to_file(move[0]);
                                    p_gantry_command->move.source_rank = utils_byte_to_rank(move[1]);
                                    p_gantry_command->move.dest_file = utils_byte_to_file(move[2]);
                                    p_gantry_command->move.dest_rank = utils_byte_to_rank(move[3]);
                                    p_gantry_command->move.move_type = utils_byte_to_move_type(move[4]);
                                }
                                else
                                {
                                    // When both moves continue the game, proceed as usual
                                    p_gantry_command->game_status = ONGOING;
                                    p_gantry_command->move.source_file = utils_byte_to_file(move[0]);
                                    p_gantry_command->move.source_rank = utils_byte_to_rank(move[1]);
                                    p_gantry_command->move.dest_file = utils_byte_to_file(move[2]);
                                    p_gantry_command->move.dest_rank = utils_byte_to_rank(move[3]);
                                    p_gantry_command->move.move_type = utils_byte_to_move_type(move[4]);
                                }
                                robot_is_done = true;
                            }
                        }
                    }
                }
                else if (instr == ILLEGAL_MOVE_INSTR)
                {
                    // Still player's turn; robot will not move.
                    message[0] = first_byte;
                    message[1] = instr_op_len;
                    rpi_receive(check_bytes, 2);
                    if (utils_validate_transmission((uint8_t *) message, 2, check_bytes))
                    {
                        rpi_transmit_ack();
                        p_gantry_command->move.move_type = IDLE;
                        robot_is_done = true;
                    }
                }
            }
        }
    }
#endif /* THREE_PARTY_MODE */
}

/**
 * @brief Interprets the RPi's move, and adds the appropriate commands to the queue
 *        Additionally, waits for the game status from RPi.
 *
 * @param command The gantry command being run
 */
void gantry_robot_exit(command_t* command)
{
    gantry_command_t* p_gantry_command = (gantry_command_t*) command;
    
    chess_move_t rook_move;

    // We should have the necessary data from the action function

    // TODO: Fill this in to load the next commands. Very rough pseudocode follows!
    // lc = led_build_command()
    // command_queue_push(lc)
    
     switch (p_gantry_command->move.move_type) {
         case MOVE:
             // Error checking
             if (p_gantry_command->move.source_file == FILE_ERROR || p_gantry_command->move.source_rank == RANK_ERROR)
             {
                 break;
             }
             // Move to the piece to move
             // the enums are the absolute positions of those ranks/file. current_pos is also absolute
             command_queue_push
             (
                 (command_t*)stepper_build_chess_xy_command
                 (
                     p_gantry_command->move.source_file,  // file
                     p_gantry_command->move.source_rank,  // rank
                     1,                                   // v_x
                     1                                    // v_y
                 )
             );
             // wait
             command_queue_push((command_t*)delay_build_command(1000));
             // lower the lifter
             // grab the piece
             // raise the lifter
             // move to the dest
             // the enums are the absolute positions of those ranks/file. current_pos is also absolute
             command_queue_push
             (
                 (command_t*)stepper_build_chess_xy_command
                 (
                     p_gantry_command->move.dest_file, // file
                     p_gantry_command->move.dest_rank, // rank
                     1,                                // v_x
                     1                                 // v_y
                 )
             );
             // wait
             command_queue_push((command_t*)delay_build_command(1000));
             // lower the lifter
             // release the piece
             // raise the lifter
             // home

             gantry_home();
         break;

         case PROMOTION:
             // TODO
             // Error checking
              if (p_gantry_command->move.source_file == FILE_ERROR || p_gantry_command->move.source_rank == RANK_ERROR)
              {
                  break;
              }

              // Go to the promoted pawn's tile
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.source_file, // file
                      p_gantry_command->move.source_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));

              // Take the pawn being promoted off the board
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      CAPTURE_FILE, // file
                      CAPTURE_RANK, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));

              // Go to the magical **queen tile**
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      CAPTURE_FILE, // file // TODO
                      CAPTURE_RANK, // rank // TODO
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));

              // Move the queen to the destination tile
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.dest_file, // file
                      p_gantry_command->move.dest_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));

              gantry_home();
         break;
        
         case CAPTURE:
             // Error checking
              if (p_gantry_command->move.source_file == FILE_ERROR || p_gantry_command->move.source_rank == RANK_ERROR)
              {
                  break;
              }
              // Move to the piece to move
              // the enums are the absolute positions of those ranks/file. current_pos is also absolute
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.dest_file,  // file
                      p_gantry_command->move.dest_rank,  // rank
                      1,                                   // v_x
                      1                                    // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // grab the piece
              // raise the lifter
              // move to the dest
              // the enums are the absolute positions of those ranks/file. current_pos is also absolute

              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      CAPTURE_FILE, // file
                      CAPTURE_RANK, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // grab the piece
              // raise the lifter
              // move to the dest
              // the enums are the absolute positions of those ranks/file. current_pos is also absolute

              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.source_file, // file
                      p_gantry_command->move.source_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // release the piece
              // raise the lifter
              // home

              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.dest_file, // file
                      p_gantry_command->move.dest_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // release the piece
              // raise the lifter
              // home

              gantry_home();
         break;

         case CASTLING:
             // Error checking
              if (p_gantry_command->move.source_file == FILE_ERROR || p_gantry_command->move.source_rank == RANK_ERROR)
              {
                  break;
              }
              rook_move = rpi_castle_get_rook_move(&p_gantry_command->move);
              // Move to the piece to move
              // the enums are the absolute positions of those ranks/file. current_pos is also absolute
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.source_file,  // file
                      p_gantry_command->move.source_rank,  // rank
                      1,                                   // v_x
                      1                                    // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // grab the piece
              // raise the lifter
              // move to the dest
              // the enums are the absolute positions of those ranks/file. current_pos is also absolute

              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.dest_file, // file
                      p_gantry_command->move.dest_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // grab the piece
              // raise the lifter
              // move to the dest
              // the enums are the absolute positions of those ranks/file. current_pos is also absolute

              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      rook_move.source_file, // file
                      rook_move.source_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // release the piece
              // raise the lifter
              // home

              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      rook_move.dest_file, // file
                      rook_move.dest_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));
              // lower the lifter
              // release the piece
              // raise the lifter
              // home

              gantry_home();
         break;

         case EN_PASSENT:
             // Error checking
              if (p_gantry_command->move.source_file == FILE_ERROR || p_gantry_command->move.source_rank == RANK_ERROR)
              {
                  break;
              }
              /* With an en passant capture, the captured pawn will have
               * the moving pawn's *source rank* and *destination file*. */

              // Capture the pawn being en passant'd
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.dest_file, // file
                      p_gantry_command->move.source_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));

              // Move the en passant'd pawn to the capture rank and file
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      CAPTURE_FILE, // file
                      CAPTURE_RANK, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));

              // Go to moving pawn's initial position
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.source_file, // file
                      p_gantry_command->move.source_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );
              // wait
              command_queue_push((command_t*)delay_build_command(1000));

              // Take the moving pawn to its final position
              command_queue_push
              (
                  (command_t*)stepper_build_chess_xy_command
                  (
                      p_gantry_command->move.dest_file, // file
                      p_gantry_command->move.dest_rank, // rank
                      1,                                // v_x
                      1                                 // v_y
                  )
              );

              gantry_home();

         break;

         case IDLE:
             // TODO
             // Invalid move
         break;

         default:
             // TODO
         break;
     }

    // lc = led_build_command()
    // command_queue_push(lc)
    // snc = sensor_network_build_command()
    // command_queue_push(snc)
     // Do it again !
     if (p_gantry_command->game_status == ONGOING)
     {
         command_queue_push((command_t*)gantry_human_build_command());
     }
    // Finally, update previous_board with the robot's move
    chessboard_update_robot_move(p_gantry_command->robot_move_uci);
}

/**
 * @brief Moves to the next command once a move has been received from the RPi
 * 
 * @param command The gantry command being run
 * @return true If the RPi responded
 */
bool gantry_robot_is_done(command_t* command)
{
    return robot_is_done;
}

/**
 * @brief Build a gantry_home command
 *
 * @returns Pointer to the dynamically-allocated command
 */
gantry_command_t* gantry_home_build_command(void)
{
    // The thing to return
    gantry_command_t* p_command = (gantry_command_t*)malloc(sizeof(gantry_command_t));

    // Functions
    p_command->command.p_entry   = &gantry_home_entry;
    p_command->command.p_action  = &utils_empty_function;
    p_command->command.p_exit    = &utils_empty_function;
    p_command->command.p_is_done = &gantry_home_is_done;

    // Data
    p_command->move.source_file = FILE_ERROR;
    p_command->move.source_rank = RANK_ERROR;
    p_command->move.dest_file   = FILE_ERROR;
    p_command->move.dest_rank   = RANK_ERROR;
    p_command->move.move_type   = IDLE;

    return p_command;
}

/**
 * @brief Toggles the homing flag
 *
 * @param command The gantry command being run
 */
void gantry_home_entry(command_t* command)
{
    gantry_homing = !gantry_homing;
}

/**
 * @brief Homing flag is toggled in entry, so return true always
 *
 * @param command The gantry command being run
 * @return true Always
 */
bool gantry_home_is_done(command_t* command)
{
    return true;
}

/**
 * @brief Homes the gantry system (motors all the way up, right, back -- from point-of-view of the robot)
 */
void gantry_home()
{
    // Set the homing flag
    command_queue_push((command_t*) gantry_home_build_command());

    // Home the motors with delay
    command_queue_push((command_t*) stepper_build_home_z_command());
    command_queue_push((command_t*) stepper_build_home_xy_command());
    command_queue_push((command_t*) delay_build_command(HOMING_DELAY_MS));

    // Back away from the edge
    command_queue_push((command_t*) stepper_build_rel_command(
            HOMING_X_BACKOFF,
            HOMING_Y_BACKOFF,
            HOMING_Z_BACKOFF,
            HOMING_X_VELOCITY,
            HOMING_Y_VELOCITY,
            HOMING_Z_VELOCITY
    ));

    // Clear the homing flag
    command_queue_push((command_t*) gantry_home_build_command());
}

/**
 * @brief Interrupt handler for the gantry module
 */
__interrupt void GANTRY_HANDLER(void)
{
    // Clear the interrupt flag
    clock_clear_interrupt(GANTRY_TIMER);
    
    // Check the current switch readings
    uint8_t switch_data = switch_get_reading();

    // If the emergency stop button was pressed, kill everything
//    if (switch_data & BUTTON_ESTOP)
//    {
//        gantry_kill();
//    }

    // If a limit switch was pressed, disable the appropriate motor
    gantry_limit_stop(switch_data);

    // If the start/reset button was pressed, send the appropriate "new game" signal
    if (switch_data & BUTTON_START_MASK)
    {
//        gantry_reset();
    }

    // If the home button was pressed, clear the queue and execute a homing command
    if (switch_data & BUTTON_HOME_MASK)
    {
//        gantry_home();
    }
}

/* End gantry.c */
