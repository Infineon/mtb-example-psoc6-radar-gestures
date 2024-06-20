/* ***************************************************************************
** File name: main.c
**
** Description: This is the main file for PSOC6 Gesture Code Example.
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

#include <inttypes.h>
#include <stdio.h>

#include "cy_pdl.h"
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#include "cli_task.h"
#include "resource_map.h"
#include "xensiv_bgt60trxx_mtb.h"
#include "xensiv_radar_gestures.h"

#include "xensiv_radar_data_management.h"

#define XENSIV_BGT60TRXX_CONF_IMPL
#include "radar_settings.h"


/*******************************************************************************
* Macros
********************************************************************************/

#define XENSIV_BGT60TRXX_SPI_FREQUENCY      (25000000UL)

#define NUM_SAMPLES_PER_FRAME               (XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP *\
                                             XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME *\
                                             XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS)

#define NUM_CHIRPS_PER_FRAME                XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME
#define NUM_SAMPLES_PER_CHIRP               XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP

/* RTOS tasks */
#define MAIN_TASK_NAME                      "main_task"
#define MAIN_TASK_STACK_SIZE                (configMINIMAL_STACK_SIZE * 10)
#define MAIN_TASK_PRIORITY                  (configMAX_PRIORITIES - 1)
#define PROCESSING_TASK_NAME                "processing_task"
#define PROCESSING_TASK_STACK_SIZE          (configMINIMAL_STACK_SIZE * 10)
#define PROCESSING_TASK_PRIORITY            (configMAX_PRIORITIES - 2)
#define CLI_TASK_NAME                       "cli_task"
#define CLI_TASK_STACK_SIZE                 (configMINIMAL_STACK_SIZE * 20)
#define CLI_TASK_PRIORITY                   (tskIDLE_PRIORITY)

/* Interrupt priorities */
#define GPIO_INTERRUPT_PRIORITY             (6)

#define GESTURE_HOLD_TIME                   (10) /* count value used to hold gesture before evaluating new one */


/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void main_task(void *pvParameters);
static void processing_task(void *pvParameters);
static void timer_callback(TimerHandle_t xTimer);

static int32_t init_leds(void);
static int32_t radar_init(void);
static void xensiv_bgt60trxx_interrupt_handler(void* args, cyhal_gpio_event_t event);

/*******************************************************************************
 * Local Declarations
 ********************************************************************************/
/*
 * @typedef typedef struct  ce_state_s
 * Structure containing gesture's result, value of verbose mode and timestamp
 */
typedef struct {
    inference_results_t gesture_result;
    bool verbose;
    uint32_t bookmark_timestamp;
}ce_state_s;

/*******************************************************************************
* Global Variables
********************************************************************************/
static cyhal_spi_t spi_obj;
static xensiv_bgt60trxx_mtb_t bgt60_obj;

static TaskHandle_t main_task_handler;
static TaskHandle_t processing_task_handler;
static TimerHandle_t timer_handler;
radar_data_manager_s mgr;

float32_t gesture_frame[NUM_SAMPLES_PER_CHIRP * NUM_CHIRPS_PER_FRAME * XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS];

ce_state_s ce_app_state;
extern bool gesture_detect_list[NUMBER_OF_GESTURE_CLASSES];
volatile bool is_settings_mode = false;

/*******************************************************************************
* Function Name: read_radar_data
********************************************************************************
* Summary:
* Function that reads the data from radar hardware buffer.
* This function is supplied to software buffer manager.
*
* Parameters:
*  * data: pointer to radar data
*  *num_samples: pointer to number of samples per frame
*  samples_ub: maximum number of samples to be copied at a time from owner task/caller
*
* Return:
*  int32_t: 0 if success
*
*******************************************************************************/
int32_t read_radar_data(uint16_t* data, uint32_t *num_samples, uint32_t samples_ub)
{
    if (xensiv_bgt60trxx_get_fifo_data(&bgt60_obj.dev,
            data,
            NUM_SAMPLES_PER_FRAME) == XENSIV_BGT60TRXX_STATUS_OK)
    {
        *num_samples = NUM_SAMPLES_PER_FRAME *2; /* in bytes */

        if (samples_ub < NUM_SAMPLES_PER_FRAME *2)
        {
            xensiv_bgt60trxx_soft_reset(&bgt60_obj.dev,XENSIV_BGT60TRXX_RESET_FIFO );
        }
    }

    return 0;
}

/*******************************************************************************
* Function Name: app_logic
********************************************************************************
* Summary:
* This function interprets the gesture results and prints the detected class of gesture.
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
void app_logic(inference_results_t * results)
{
    if (is_settings_mode)
    {
        return;
    }

    const char classes[][20]  = {"BACKGROUND","PUSH","SWIPE_LEFT","SWIPE_RIGHT","UNKNOWN_1","UNKNOWN_2","SWIPE_UP","SWIPE_DOWN"};
    static int gesture_hold = 0;

    if ( gesture_detect_list[results->idx] == true ) /* check if gesture is on the detect_list */
    {
        if (gesture_hold > 0)
        {
            gesture_hold += 1;
        }

        if ((results->score > gesture_detection_threshold) && (gesture_hold == 0))
        {
            cyhal_gpio_write(LED_RGB_RED, true); /* turn on red LED */
            cyhal_gpio_write(LED_RGB_GREEN, false); /* turn off green LED */

            if (!ce_app_state.verbose) /* print gesture detection in non-verbose mode */
            {
                printf("[INFO]\"class\": \"%s\", \"score\": %f\r\n", classes[results->idx], results->score);
            }
            else  /* print gesture detection in verbose mode */
            {
                ce_app_state.bookmark_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                printf("[INFO][GESTURE] %s %f %" PRIu32 "\n",  classes[results->idx], results->score, ce_app_state.bookmark_timestamp);
            }
            gesture_hold += 1;
        }

        if (gesture_hold > GESTURE_HOLD_TIME)
        {
            gesture_hold = 0;
            cyhal_gpio_write(LED_RGB_RED, false); /* turn off red LED */
            cyhal_gpio_write(LED_RGB_GREEN, true); /* turn on green LED */
        }
    }
    else
    {
        cyhal_gpio_write(LED_RGB_RED, false); /* turn off red LED */
        cyhal_gpio_write(LED_RGB_GREEN, true); /* turn on green LED */
    }
}


/*******************************************************************************
* Function Name: deinterleave_antennas
********************************************************************************
* Summary:
* This function de-interleaves multiple antennas data from single radar HW FIFO
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
void deinterleave_antennas(uint16_t * buffer_ptr)
{
    uint8_t antenna = 0;
    int32_t index = 0;
    static const float norm_factor = 1.0f;

    for (int i = 0; i < (NUM_SAMPLES_PER_CHIRP * NUM_CHIRPS_PER_FRAME * XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS); ++i)
    {
        gesture_frame[index + antenna * NUM_SAMPLES_PER_CHIRP * NUM_CHIRPS_PER_FRAME] = buffer_ptr[i] * norm_factor;
        antenna++;
        if (antenna == XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS)
        {
            antenna = 0;
            index++;
        }
    }
}


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CM4 CPU. It initializes BSP, creates FreeRTOS
* main task and starts the scheduler.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io to use the debug UART port */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

#ifdef TARGET_APP_CYSBSYSKIT_DEV_01

    /* Initialize the User LED */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

#endif

    mgr.in_read_radar_data = read_radar_data;
    radar_data_manager_init(&mgr, NUM_SAMPLES_PER_FRAME *6, NUM_SAMPLES_PER_FRAME *2);
    radar_data_manager_set_malloc_free(pvPortMalloc,
            vPortFree);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("****************** "
           "Radar gesture"
           "****************** \r\n\n"
           "Gesture detection using XENSIV 60-GHz radar\r\n"
           );

    /* Create the RTOS task */
    if (xTaskCreate(main_task, MAIN_TASK_NAME, MAIN_TASK_STACK_SIZE, NULL, MAIN_TASK_PRIORITY, &main_task_handler) != pdPASS)
    {
        CY_ASSERT(0);
    }

    /* Start the FreeRTOS scheduler. */
    vTaskStartScheduler();

    CY_ASSERT(0);
}

/*******************************************************************************
* Function Name: main_task
********************************************************************************
* Summary:
* This is the main task.
*    1. Creates a timer to toggle user LED
*    2. Create the processing RTOS task
*    3. Initializes the hardware interface to the sensor and LEDs
*    4. Initializes the radar device
*    5. Initializes gesture library
*    6. In an infinite loop
*       - Waits for interrupt from radar device indicating availability of data
*       - Read from software buffer the raw radar frame
*       - De-interleaves the radar data frame
*       - Acknowledges the radar data manager the consumption of read data
*       - Sends notification to processing task 
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
static __NO_RETURN void main_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t sz;

    uint16_t *data_buff = NULL;

    timer_handler = xTimerCreate("timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, timer_callback);
    if (timer_handler == NULL)
    {
        CY_ASSERT(0);
    }

    if (xTimerStart(timer_handler, 0) != pdPASS)
    {
        CY_ASSERT(0);
    }

    if (xTaskCreate(processing_task, PROCESSING_TASK_NAME, PROCESSING_TASK_STACK_SIZE, NULL, PROCESSING_TASK_PRIORITY, &processing_task_handler) != pdPASS)
    {
        CY_ASSERT(0);
    }

    if (radar_init() != 0)
    {
        CY_ASSERT(0);
    }

    if (init_leds () != 0)
    {
        CY_ASSERT(0);
    }

    mgr.subscribe(main_task_handler);

    /* Initialize the initial state of ce_app_state */
    ce_app_state.gesture_result.idx = 0;
    ce_app_state.gesture_result.score = 0;
    ce_app_state.bookmark_timestamp = 0;
    ce_app_state.verbose = false;

    if (xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        CY_ASSERT(0);
    }

    gestures_init();

    for(;;)
    {
        /* Wait for the GPIO interrupt to indicate that another slice is available */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        mgr.read_from_buffer(1, &data_buff, &sz);

        deinterleave_antennas(data_buff);

        mgr.ack_data_read(1);

        /* Tell processing task to take over */
        xTaskNotifyGive(processing_task_handler);

    }
}

/*******************************************************************************
* Function Name: processing_task
********************************************************************************
* Summary:
* This is the data processing task.
*    1. It creates a console task to handle parameter configuration for the library
*    2. In a loop
*       - wait for the frame data available for process
*       - Runs the Gesture algorithm and provides the result 
*       - Interprets the results using app_logic() call
*
* Parameters:
*  void
*
* Return:
*  None
*
*******************************************************************************/
static __NO_RETURN void processing_task(void *pvParameters)
{
    (void)pvParameters;
    inference_results_t results;

    if (xTaskCreate(console_task, CLI_TASK_NAME, CLI_TASK_STACK_SIZE, NULL, CLI_TASK_PRIORITY, NULL) != pdPASS)
    {
        CY_ASSERT(0);
    }

    for(;;)
    {
        /* Wait for frame data available to process */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /*pass on the de-interleaved data on to Algorithmic kernel*/
        gestures_run(gesture_frame, &results);

        /*interpret results*/
        app_logic(&results);

    }
}


/*******************************************************************************
* Function Name: radar_init
********************************************************************************
* Summary:
* This function configures the SPI interface, initializes radar and interrupt
* service routine to indicate the availability of radar data. 
* 
* Parameters:
*  void
*
* Return:
*  Success or error 
*
*******************************************************************************/
static int32_t radar_init(void)
{
    if (cyhal_spi_init(&spi_obj,
                       PIN_XENSIV_BGT60TRXX_SPI_MOSI,
                       PIN_XENSIV_BGT60TRXX_SPI_MISO,
                       PIN_XENSIV_BGT60TRXX_SPI_SCLK,
                       NC,
                       NULL,
                       8,
                       CYHAL_SPI_MODE_00_MSB,
                       false) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: cyhal_spi_init failed\n");
        return -1;
    }

    /* Reduce drive strength to improve EMI */
    Cy_GPIO_SetSlewRate(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CY_GPIO_DRIVE_1_8);
    Cy_GPIO_SetSlewRate(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CY_GPIO_DRIVE_1_8);

    /* Set the data rate to 25 Mbps */
    if (cyhal_spi_set_frequency(&spi_obj, XENSIV_BGT60TRXX_SPI_FREQUENCY) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: cyhal_spi_set_frequency failed\n");
        return -1;
    }

    /* Enable LDO */
    if (cyhal_gpio_init(PIN_XENSIV_BGT60TRXX_LDO_EN,
                        CYHAL_GPIO_DIR_OUTPUT,
                        CYHAL_GPIO_DRIVE_STRONG,
                        true) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: LDO_EN cyhal_gpio_init failed\n");
        return -1;
    }

    /* Wait LDO stable */
    (void)cyhal_system_delay_ms(5);

    if (xensiv_bgt60trxx_mtb_init(&bgt60_obj, 
                                  &spi_obj, 
                                  PIN_XENSIV_BGT60TRXX_SPI_CSN, 
                                  PIN_XENSIV_BGT60TRXX_RSTN, 
                                  register_list,
                                  XENSIV_BGT60TRXX_CONF_NUM_REGS) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: xensiv_bgt60trxx_mtb_init failed\n");
        return -1;
    }

    if (xensiv_bgt60trxx_mtb_interrupt_init(&bgt60_obj,
                                            NUM_SAMPLES_PER_FRAME*2,
                                            PIN_XENSIV_BGT60TRXX_IRQ,
                                            GPIO_INTERRUPT_PRIORITY,
                                            xensiv_bgt60trxx_interrupt_handler,
                                            NULL) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: xensiv_bgt60trxx_mtb_interrupt_init failed\n");
        return -1;
    }

    return 0;
}


/*******************************************************************************
* Function Name: xensiv_bgt60trxx_interrupt_handler
********************************************************************************
* Summary:
* This is the interrupt handler to react on sensor indicating the availability 
* of new data
*    1. Triggers the radar data manager for buffering radar data into software buffer.
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
#if defined(CYHAL_API_VERSION) && (CYHAL_API_VERSION >= 2)
static void xensiv_bgt60trxx_interrupt_handler(void *args, cyhal_gpio_event_t event)
#else
static void xensiv_bgt60trxx_interrupt_handler(void *args, cyhal_gpio_irq_event_t event)
#endif
{
    CY_UNUSED_PARAMETER(args);
    CY_UNUSED_PARAMETER(event);

    mgr.run(true);
}


/*******************************************************************************
* Function Name: init_leds
********************************************************************************
* Summary:
* This function initializes the GPIOs for LEDs and set them to off state.
* Parameters:
*  void
*
* Return:
*  Success or error
*
*******************************************************************************/
static int32_t init_leds(void)
{

    if(cyhal_gpio_init(LED_RGB_RED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, false)!= CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: GPIO initialization for LED_RGB_RED failed\n");
        return -1;
    }

    if( cyhal_gpio_init(LED_RGB_GREEN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, false)!= CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: GPIO initialization for LED_RGB_GREEN failed\n");
        return -1;
    }

    if( cyhal_gpio_init(LED_RGB_BLUE, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, false)!= CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: GPIO initialization for LED_RGB_BLUE failed\n");
        return -1;
    }

    return 0;
}


/*******************************************************************************
* Function Name: timer_callback
********************************************************************************
* Summary:
* This is the timer_callback which toggles the LED
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
static void timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;

#ifdef TARGET_APP_CYSBSYSKIT_DEV_01
    cyhal_gpio_toggle(CYBSP_USER_LED);
#endif
}

/* [] END OF FILE */
