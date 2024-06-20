/*****************************************************************************
 * File name: cli_task.c
 *
 * Description: This file implements a terminal UI to configure parameters
 ** for radar gesture application.

 *
 * ===========================================================================
 * Copyright (C) 2023 Infineon Technologies AG. All rights reserved.
 * ===========================================================================
 *
 * ===========================================================================
 * Infineon Technologies AG (INFINEON) is supplying this file for use
 * exclusively with Infineon's sensor products. This file can be freely
 * distributed within development tools and software supporting such
 * products.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
 * OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
 * INFINEON SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES, FOR ANY REASON
 * WHATSOEVER.
 * ===========================================================================
 */

#include <stdlib.h>

#include "cy_retarget_io.h"

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "cli_task.h"
#include "xensiv_radar_gestures.h"
#include "resource_map.h"
#include "cyhal_gpio.h"

/*******************************************************************************
 * Macros
 ********************************************************************************/
#define NUMBER_OF_COMMANDS (5)

/* Strings length */
#define MAX_INPUT_LENGTH              (100)
#define MAX_OUTPUT_LENGTH             (100)
#define MAX_GESTURE_STRING_LENGTH     (12) /* based on SWIPE_RIGHT (11 char + 1) */

/* Strings for enable and disable */
#define ENABLE_STRING  ("enable")
#define DISABLE_STRING ("disable")

/* Keyboard keys */
#define ENTER_KEY     (0x0D)
#define ESC_KEY       (0x1B)
#define BACKSPACE_KEY (0x08)

/* Names for supported gestures */
#define GESTURE_PUSH_STRING            ("PUSH")
#define GESTURE_SWIPE_LEFT_STRING      ("SWIPE_LEFT")
#define GESTURE_SWIPE_RIGHT_STRING     ("SWIPE_RIGHT")
#define GESTURE_SWIPE_DOWN_STRING      ("SWIPE_DOWN")
#define GESTURE_SWIPE_UP_STRING        ("SWIPE_UP")
#define GESTURE_ALL_STRING             ("ALL")

/*******************************************************************************
 * Local Declarations
 ********************************************************************************/
typedef struct {
    inference_results_t gesture_result;
    bool verbose;
    uint32_t bookmark_timestamp;
}ce_state_s;

typedef enum
{
    XENSIV_RADAR_GESTURE_BACKGROUND,
    XENSIV_RADAR_GESTURE_PUSH,
    XENSIV_RADAR_GESTURE_SWIPE_LEFT,
    XENSIV_RADAR_GESTURE_SWIPE_RIGHT,
    XENSIV_RADAR_GESTURE_UNKNOWN_1,
    XENSIV_RADAR_GESTURE_UNKNOWN_2,
    XENSIV_RADAR_GESTURE_SWIPE_UP,
    XENSIV_RADAR_GESTURE_SWIPE_DOWN,
    XENSIV_RADAR_GESTURE_ALL = 20
} xensiv_radar_gestures_class_e;

/*******************************************************************************
 * Function Prototypes
 ********************************************************************************/
static BaseType_t set_gestures_detect_list(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString);
static BaseType_t display_gestures_list(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString);
static BaseType_t display_board_Info(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString);
static BaseType_t display_solution_config(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString);
static BaseType_t set_verbose(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString);
static inline bool check_bool_validation(const char *value, const char *enable,
        const char *disable);
static inline bool string_to_bool(const char *string, const char *enable,
        const char *disable);
static inline bool check_supported_gesture_validation(char *gesture);
static inline xensiv_radar_gestures_class_e string_to_gesture(char *gesture);

/*******************************************************************************
 * Variables
 ********************************************************************************/
static CLI_Command_Definition_t command_list[NUMBER_OF_COMMANDS] =
{
    {
        .pcCommand = "verbose",
        .pcHelpString = "verbose <enable|disable> - Enable/disable detailed verbose status to be updated every second\n",
        .pxCommandInterpreter = set_verbose,
        .cExpectedNumberOfParameters = 1
    },
    {
        .pcCommand = "board_info",
        .pcHelpString = "board_info -  Board_Information\n",
        .pxCommandInterpreter = display_board_Info,
        .cExpectedNumberOfParameters = 0
    },
    {
        .pcCommand = "config",
        .pcHelpString = "config - solution configuration information\n",
        .pxCommandInterpreter = display_solution_config,
        .cExpectedNumberOfParameters = 0
    },
    {
        .pcCommand = "gestures_list",
        .pcHelpString = "gestures_list - display all supported gestures\n",
        .pxCommandInterpreter = display_gestures_list,
        .cExpectedNumberOfParameters = 0
    },
    {
        .pcCommand = "gestures_detect",
        .pcHelpString = "gestures_detect <Gestures|ALL> \r\n eg: gestures_detect PUSH SWIPE_UP - enable PUSH & SWIPE UP\r\n",
        .pxCommandInterpreter = set_gestures_detect_list,
        .cExpectedNumberOfParameters = -1 /* variable no. of parameters */
    }
};

bool gesture_detect_list[NUMBER_OF_GESTURE_CLASSES]  = {false, true, true, true, false, false, true, true};
extern ce_state_s ce_app_state;
extern volatile bool is_settings_mode;

/*******************************************************************************
 * Function Name: console_task
 ********************************************************************************
 * Summary:
 * This is the console task.
 *    1. Register commands
 *    2. In loop there are two modes: primary mode all gestures are enabled and setting mode a particular gesture or group of gestures can be enabled.
 *       - Waits for a sign
 *       - When ENTER is hit go to the settings mode
 *       - Use 'help' command to know what commands are available
 *       - Type commands with values to change parameters
 *       - Press ESC to exit settings mode and go again to gesture mode
 *
 * Parameters:
 *  void
 *
 * Return:
 *  None
 *
 *******************************************************************************/
__NO_RETURN void console_task(void *pvParameters)
{
    int8_t cInputIndex = 0;
    BaseType_t xMoreDataToFollow;
    /* The input and output buffers are declared static to keep them off the stack. */
    static char pcOutputString[MAX_OUTPUT_LENGTH];
    static char pcInputString[MAX_INPUT_LENGTH];
    bool setting_mode = false;


    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (int32_t i = 0; i < NUMBER_OF_COMMANDS; ++i)
    {
        FreeRTOS_CLIRegisterCommand(&command_list[i]);
    }

    for (;;)
    {
        /* Wait for a sign */
        int32_t c = getchar();

        if (!setting_mode)
        {
            /* Enter setting mode */
            if (c == ENTER_KEY)
            {
                is_settings_mode = true;
                cInputIndex = 0;
                memset(pcInputString, 0x00, MAX_INPUT_LENGTH);
                setting_mode = true;
                printf("\r\nEnter settings mode\r\n"
                        "> ");
                cyhal_gpio_write(LED_RGB_RED, false); /* turn off red LED */
                cyhal_gpio_write(LED_RGB_GREEN, false); /* turn off green LED */
                cyhal_gpio_write(LED_RGB_BLUE, true); /* turn on blue LED */
            }
        }
        else
        {
            /* Exit setting mode */
            if (c == ESC_KEY)
            {
                is_settings_mode = false;
                cInputIndex = 0;
                memset(pcInputString, 0x00, MAX_INPUT_LENGTH);
                setting_mode = false;
                printf("\r\nQuit from settings menu\r\n\n");
                cyhal_gpio_write(LED_RGB_RED, false); /* turn off red LED */
                cyhal_gpio_write(LED_RGB_GREEN, true); /* turn on green LED */
                cyhal_gpio_write(LED_RGB_BLUE, false); /* turn off blue LED */
            }
            else if (c == ENTER_KEY) /* confirm entered text */
            {
                putchar('\n');

                /* The command interpreter is called repeatedly until it returns
                 pdFALSE.  See the "Implementing a command" documentation for an
                 explanation of why this is. */
                do {
                    /* Send the command string to the command interpreter.  Any
                     output generated by the command interpreter will be placed in the
                     pcOutputString buffer. */
                    xMoreDataToFollow = FreeRTOS_CLIProcessCommand(
                            pcInputString, /* The command string.*/
                            pcOutputString, /* The output buffer. */
                            MAX_OUTPUT_LENGTH /* The size of the output buffer. */
                    );

                    /* Write the output generated by the command interpreter to the
                     console. */
                    printf("%s", pcOutputString);
                } while (xMoreDataToFollow != pdFALSE);

                /* All the strings generated by the input command have been sent.
                 Processing of the command is complete.  Clear the input string ready
                 to receive the next command. */
                cInputIndex = 0;
                memset(pcInputString, 0x00, MAX_INPUT_LENGTH);

                printf("> ");
            }
            else
            {
                if (c == BACKSPACE_KEY)
                {
                    /* Backspace was pressed.  Erase the last character in the input
                     buffer - if there are any. */
                    if (cInputIndex > 0)
                    {
                        cInputIndex--;
                        pcInputString[cInputIndex] = ' ';
                        putchar(BACKSPACE_KEY);
                    }
                }
                else
                {
                    /* A character was entered.  It was not a new line, backspace
                     or carriage return, so it is accepted as part of the input and
                     placed into the input buffer.  When a \n is entered the complete
                     string will be passed to the command interpreter. */
                    if (cInputIndex < MAX_INPUT_LENGTH)
                    {
                        pcInputString[cInputIndex] = c;
                        cInputIndex++;
                        putchar(c);
                    }
                }
            }
        }
    }
}

/*******************************************************************************
 * Function Name: set_verbose
 ********************************************************************************
 * Summary:
 *   Enabling/disabling verbose mode
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t set_verbose(char *pcWriteBuffer,
        size_t xWriteBufferLen, const char *pcCommandString)
{
    const char *pcParameter;
    BaseType_t lParameterStringLength;

    configASSERT(pcWriteBuffer);

    /* Obtain the parameter string. */
    pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
            1, /* Return the first parameter. */
            &lParameterStringLength); /* Store the parameter string length. */

    configASSERT(pcParameter);
    if (check_bool_validation(pcParameter, ENABLE_STRING, DISABLE_STRING))
    {
        ce_app_state.verbose = string_to_bool(pcParameter,
                ENABLE_STRING, DISABLE_STRING);
        sprintf(pcWriteBuffer, "ok\n");
    }
    else
    {
        sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
    }

    return pdFALSE;

}

/*******************************************************************************
 * Function Name: set_gestures_detect_list
 ********************************************************************************
 * Summary:
 *   Setting mode for gesture detection algorithm
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t set_gestures_detect_list(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString)
{
    const char *pcParameter;
    char gesture_array[MAX_GESTURE_STRING_LENGTH] = {0};
    BaseType_t lParameterStringLength;
    uint8_t param_count;
    xensiv_radar_gestures_class_e gesture_idx;
    bool update_list[NUMBER_OF_GESTURE_CLASSES]  = {false, false, false, false, false, false, false, false};
    uint8_t i=0;
    bool first_print = true;

    configASSERT(pcWriteBuffer);

    for ( param_count=1; param_count<=xWriteBufferLen; param_count++ )
    {
        /* Obtain the parameter string. */
        pcParameter = FreeRTOS_CLIGetParameter(pcCommandString, /* The command string itself. */
                param_count,
                &lParameterStringLength); /* Store the parameter string length. */

        configASSERT(pcParameter);

        memset(gesture_array, 0, MAX_GESTURE_STRING_LENGTH);
        for ( i=0; i<lParameterStringLength; i++)
        {
            gesture_array[i] = *pcParameter;
            pcParameter++;
        }

        if (check_supported_gesture_validation(&gesture_array[0]))
        {
            if( first_print )
            {
                printf(CONFIG_GESTURES_DETECT);
                first_print = false;
            }

            gesture_idx = string_to_gesture(&gesture_array[0]);
            switch ( gesture_idx )
            {
                case XENSIV_RADAR_GESTURE_PUSH:
                    update_list[XENSIV_RADAR_GESTURE_PUSH] = 1;
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_LEFT:
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_LEFT] = 1;
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_RIGHT:
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_RIGHT] = 1;
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_UP:
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_UP] = 1;
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_DOWN:
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_DOWN] = 1;
                    break;
                case XENSIV_RADAR_GESTURE_ALL: default:
                    update_list[XENSIV_RADAR_GESTURE_PUSH] = 1;
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_LEFT] = 1;
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_RIGHT] = 1;
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_UP] = 1;
                    update_list[XENSIV_RADAR_GESTURE_SWIPE_DOWN] = 1;
                    break;
            }
        }
        else
        {
            sprintf(pcWriteBuffer, "Invalid value.\r\n\n");
        }
    }

    /* update gesture detect list */
    for ( i=0; i<NUMBER_OF_GESTURE_CLASSES; i++ )
    {
        if ( update_list[i] )
        {
            switch ( i )
            {
                case XENSIV_RADAR_GESTURE_PUSH:
                    printf(GESTURE_PUSH_STRING);
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_LEFT:
                    printf(GESTURE_SWIPE_LEFT_STRING);
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_RIGHT:
                    printf(GESTURE_SWIPE_RIGHT_STRING);
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_UP:
                    printf(GESTURE_SWIPE_UP_STRING);
                    break;
                case XENSIV_RADAR_GESTURE_SWIPE_DOWN:
                    printf(GESTURE_SWIPE_DOWN_STRING);
                    break;
                default:
                    break;
            }
            printf(" ");
        }
        sprintf(pcWriteBuffer, "\n");
        gesture_detect_list[i] = update_list[i];
    }

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: display_gestures_list
 ********************************************************************************
 * Summary:
 *   Query for supported gestures information
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t display_gestures_list(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString)
{
    printf(CONFIG);
    printf("\n");
    printf(CONFIG_GESTURES_LIST);
    printf(GESTURE_PUSH_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_LEFT_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_RIGHT_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_UP_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_DOWN_STRING);
    printf("\n");
    printf(CONFIG);
    sprintf(pcWriteBuffer, "\n");

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: display_board_Info
 ********************************************************************************
 * Summary:
 *   Query for board information
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/
static BaseType_t display_board_Info(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString)
{
    printf(BOARD_INFO);
    printf("\n");
    printf(BOARD_INFO_APPLICATION);
    printf("\n");
    printf(BOARD_INFO_FIRMWARE);
    printf("\n");
    printf(BOARD_INFO_DEVICE_NAME);
    printf("\n");
    printf(BOARD_INFO_DEVICE_VERSION);
    printf("\n");
    printf(BOARD_INFO);
    sprintf(pcWriteBuffer, "\n");

    return pdFALSE;
}

/*******************************************************************************

 * Function Name: display_solution_config
 ********************************************************************************
 * Summary:
 *   display solution configuration
 *
 * Parameters:
 *   pcWriteBuffer: buffer into which the output from executing the command can be written
 *   xWriteBufferLen:length, in bytes of the pcWriteBuffer buffer
 *   pcCommandString: entire string as input by
 the user (from which parameters can be extracted)
 *
 * Return:
 *   pdFALSE indicating that the function ends it's processing
 *******************************************************************************/

static BaseType_t display_solution_config(char *pcWriteBuffer, size_t xWriteBufferLen,
        const char *pcCommandString)
{
    printf(CONFIG);
    printf("\n");
    printf(CONFIG_GESTURES_LIST);
    printf(GESTURE_PUSH_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_LEFT_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_RIGHT_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_UP_STRING);
    printf(" ");
    printf(GESTURE_SWIPE_DOWN_STRING);
    printf("\n");
    printf(CONFIG_GESTURES_DETECT);
    if (gesture_detect_list[XENSIV_RADAR_GESTURE_PUSH] == true)
    {
        printf(GESTURE_PUSH_STRING);
        printf(" ");
    }
    if (gesture_detect_list[XENSIV_RADAR_GESTURE_SWIPE_LEFT] == true)
    {
        printf(GESTURE_SWIPE_LEFT_STRING);
        printf(" ");
    }
    if (gesture_detect_list[XENSIV_RADAR_GESTURE_SWIPE_RIGHT] == true)
    {
        printf(GESTURE_SWIPE_RIGHT_STRING);
        printf(" ");
    }
    if (gesture_detect_list[XENSIV_RADAR_GESTURE_SWIPE_UP] == true)
    {
        printf(GESTURE_SWIPE_UP_STRING);
        printf(" ");
    }
    if (gesture_detect_list[XENSIV_RADAR_GESTURE_SWIPE_DOWN] == true)
    {
        printf(GESTURE_SWIPE_DOWN_STRING);
    }
    printf("\n");
    printf(CONFIG);
    sprintf(pcWriteBuffer, "\n");

    return pdFALSE;
}

/*******************************************************************************
 * Function Name: check_bool_validation
 ********************************************************************************
 * Summary:
 *   Checks if entered value is a proper string for enable or disable
 *
 * Parameters:
 *   value : Entered string value
 *   enable : String value for enable
 *   disable : String value for disable
 *
 * Return:
 *   True if the value is same as enable or disable value, false if it is different
 *******************************************************************************/
static inline bool check_bool_validation(const char *value, const char *enable,
        const char *disable)
{
    bool result = false;

    if ((strcmp(value, enable) == 0) || (strcmp(value, disable) == 0))
    {
        result = true;
    }
    else
    {
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Function Name: string_to_bool
 ********************************************************************************
 * Summary:
 *   Converts string value to true or false
 *
 * Parameters:
 *   string : Entered string value
 *   enable : String for enable value
 *   disable : String for disable value
 *
 * Return:
 *   True if entered string is enable, false if disable
 *******************************************************************************/
static inline bool string_to_bool(const char *string, const char *enable,
        const char *disable)
{
    CY_ASSERT((strcmp(string, enable) == 0) || (strcmp(string, disable) == 0));
    bool result = false;

    if (strcmp(string, enable) == 0)
    {
        result = true;
    }
    else
    {
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Function Name: check_supported_gesture_validation
 ********************************************************************************
 * Summary:
 *   Checks if entered value is a supported gesture value
 *
 * Parameters:
 *   value : Entered gesture value
 *
 * Return:
 *   True if the value is correct, false if the value is incorrect
 *******************************************************************************/
static inline bool check_supported_gesture_validation(char *gesture)
{
    bool result = false;

    if ((strcmp(gesture, GESTURE_PUSH_STRING) == 0)
            || (strcmp(gesture, GESTURE_SWIPE_LEFT_STRING) == 0)
            || (strcmp(gesture, GESTURE_SWIPE_RIGHT_STRING) == 0)
            || (strcmp(gesture, GESTURE_SWIPE_UP_STRING) == 0)
            || (strcmp(gesture, GESTURE_SWIPE_DOWN_STRING) == 0)
            || (strcmp(gesture, GESTURE_ALL_STRING) == 0))
    {
        result = true;
    }
    else
    {
        result = false;
    }

    return result;
}

/*******************************************************************************
 * Function Name: update_detected_gesture_list
 ********************************************************************************
 * Summary:
 *   Translates string value into a numeral value for mode
 *
 * Parameters:
 *   value : Entered string
 *
 * Return:
 *   gesture mode value
 *******************************************************************************/
static inline xensiv_radar_gestures_class_e string_to_gesture(char *gesture)
{
    xensiv_radar_gestures_class_e result = XENSIV_RADAR_GESTURE_ALL;

    if (strcmp(gesture, GESTURE_PUSH_STRING) == 0)
    {
        result = XENSIV_RADAR_GESTURE_PUSH;
    }
    else if (strcmp(gesture, GESTURE_SWIPE_LEFT_STRING) == 0)
    {
        result = XENSIV_RADAR_GESTURE_SWIPE_LEFT;
    }
    else if (strcmp(gesture, GESTURE_SWIPE_RIGHT_STRING) == 0)
    {
        result = XENSIV_RADAR_GESTURE_SWIPE_RIGHT;
    }
    else if (strcmp(gesture, GESTURE_SWIPE_DOWN_STRING) == 0)
    {
        result = XENSIV_RADAR_GESTURE_SWIPE_DOWN;
    }
    else if (strcmp(gesture, GESTURE_SWIPE_UP_STRING) == 0)
    {
        result = XENSIV_RADAR_GESTURE_SWIPE_UP;
    }
    else if (strcmp(gesture, GESTURE_ALL_STRING) == 0)
    {
        result = XENSIV_RADAR_GESTURE_ALL;
    }
    else
    {

    }
    return result;
}

