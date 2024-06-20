/******************************************************************************
** File name: cli_task.h
**
** Description: This file contains the function prototypes and constants used
**   in cli_task.c.
**
** ===========================================================================
** Copyright (C) 2023 Infineon Technologies AG. All rights reserved.
** ===========================================================================
**
** ===========================================================================
** Infineon Technologies AG (INFINEON) is supplying this file for use
** exclusively with Infineon's sensor products. This file can be freely
** distributed within development tools and software supporting such
** products.
**
** THIS SOFTWARE IS PROVIDED "AS IS".  NO WARRANTIES, WHETHER EXPRESS, IMPLIED
** OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF
** MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE.
** INFINEON SHALL NOT, IN ANY CIRCUMSTANCES, BE LIABLE FOR DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES, FOR ANY REASON
** WHATSOEVER.
** ===========================================================================
*/

#ifndef CLI_TASK_H_
#define CLI_TASK_H_


/*******************************************************************************
 * Functions
 *******************************************************************************/

__NO_RETURN void console_task(void *pvParameters);

#define NUMBER_OF_GESTURE_CLASSES (8)

#define BOARD_INFO                     ("[BOARD_INFO]")
#define BOARD_INFO_APPLICATION         ("[BOARD_INFO] application XENSIV Radar Gestures")
#define BOARD_INFO_FIRMWARE            ("[BOARD_INFO] firmware 0.5.0")

#ifdef TARGET_APP_KIT_BGT60TR13C_EMBEDD
#define BOARD_INFO_DEVICE_NAME         ("[BOARD_INFO] device_name KIT-BGT60TR13C-EMBEDD")
#elif defined(TARGET_APP_CYSBSYSKIT_DEV_01)
#define BOARD_INFO_DEVICE_NAME         ("[BOARD_INFO] device_name CYSBSYSKIT-DEV-01")
#endif

#define BOARD_INFO_DEVICE_VERSION      ("[BOARD_INFO] device_version 1.0.0")


#define CONFIG                         ("[CONFIG]")
#define CONFIG_GESTURES_LIST           ("[CONFIG] gestures_list ")
#define CONFIG_GESTURES_DETECT         ("[CONFIG] gestures_detect ")


#define MSG                            ("[MSG]")
#define MSG_TYPE_ERROR                 ("[MSG] ERROR ")




#endif /* CLI_TASK_H_ */
