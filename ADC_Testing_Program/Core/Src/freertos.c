/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "uut_task.h"
#include "pc_test_uut.h"
#include "queue.h"
#include "lwip/udp.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define QUEUE_COUNT     5
#define QUEUE_ITEM_SIZE sizeof(packet_t)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
TaskHandle_t  adcTaskHandle;
QueueHandle_t adcQueueHandle;
uint16_t      g_pc_port;
ip_addr_t     g_pc_ip_addr;
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void StartADCTask(void *argument);
void udp_listener_init(void);
void udp_receive_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

extern void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
	/* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
	/* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
	/* add queues, ... */
	adcQueueHandle = xQueueCreate(QUEUE_COUNT, QUEUE_ITEM_SIZE);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
	/* add threads, ... */
	xTaskCreate(StartADCTask,
				"ADCTask",
				configMINIMAL_STACK_SIZE * 4,
	            NULL, osPriorityNormal1,
				&adcTaskHandle);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
	/* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN StartDefaultTask */
	/* Initialize UDP Listener */
	udp_listener_init();
	/* Infinite loop */
    for (;;) {
        osDelay(1);
    }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/**
 * @brief  Initializes the lwIP UDP listener on UUT_PORT.
 *
 * Must be called from defaultTask only (lwIP context).
 * Registers udp_receive_callback to handle all incoming packets.
 */
void udp_listener_init(void)
{
    struct udp_pcb *upcb = udp_new();

    printf("UUT: Setting up UDP listener on port %d...\r\n", UUT_PORT);

    if (upcb == NULL) {
        printf("ERROR: Could not allocate UDP PCB\r\n");
        return;
    }

    err_t err = udp_bind(upcb, IP_ADDR_ANY, UUT_PORT);
    if (err != ERR_OK) {
        printf("ERROR: UDP bind failed (err=%d)\r\n", err);
        return;
    }

    udp_recv(upcb, udp_receive_callback, NULL);
    printf("UUT: UDP listener ready\r\n");
}

/**
 * @brief  UDP receive callback — called by lwIP when a packet arrives.
 *
 * Validates the minimum packet size and CRC, then routes the command
 * to the appropriate peripheral queue.
 *
 * @param  arg   Unused.
 * @param  upcb  UDP PCB that received the packet (unused).
 * @param  p     Received pbuf chain — must be freed before returning.
 * @param  addr  Source IP address of the sender.
 * @param  port  Source port of the sender — saved to g_pc_port.
 */
void udp_receive_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)upcb;

    if (p == NULL) {
        return;
    }

    packet_t *rcv_packet = (packet_t *)p->payload;
    printf("\r\nNetwork: packet received (opcode=%d, length=%d)\r\n",
           rcv_packet->opcode, rcv_packet->length);

    /* Minimum packet: Opcode(1) + Length(2) + CRC(4) = 7 bytes */
    if (p->len < 7) {
        printf("WARNING: packet too small (length=%d)\r\n", p->len);
        pbuf_free(p);
        return;
    }

    g_pc_port    = port;
    g_pc_ip_addr = *addr;

    /* Validate CRC */
    uint32_t payload_len = p->len - 4;
    uint8_t *payload_ptr = (uint8_t *)p->payload;
    uint32_t rx_crc, calc_crc;
    memcpy(&rx_crc, payload_ptr + payload_len, 4);
    calc_crc = UUT_ComputeCRC(payload_ptr, payload_len);

    if (calc_crc != rx_crc) {
        printf("WARNING: CRC mismatch (rx=%lu, calc=%lu)\r\n", rx_crc, calc_crc);
        pbuf_free(p);
        return;
    }

    /* Copy packet and free pbuf */
    packet_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint16_t safe_len = (payload_len > sizeof(cmd)) ? sizeof(cmd) : (uint16_t)payload_len;
    memcpy(&cmd, p->payload, safe_len);
    pbuf_free(p);

    /* Route to the correct peripheral queue */
    if (cmd.opcode == ADC_TEST) {
        if (xQueueSendToBack(adcQueueHandle, &cmd, 0) != pdPASS) {
            printf("WARNING: ADC queue full!\r\n");
        }
    }
}
/* USER CODE END Application */

