/**
 * @file    uut_task.h
 * @brief   Shared types, macros and API for all UUT peripheral test tasks.
 *
 * This header is included by every peripheral task file (uart_task.c,
 * spi_task.c, i2c_task.c, adc_task.c, timer_task.c) and provides:
 *
 *   - udp_send_req_t  : heap-allocated UDP send request passed to tcpip_callback
 *   - uut_send_result : thread-safe UDP result sender (call from any task)
 */

#ifndef INC_UUT_TASK_H_
#define INC_UUT_TASK_H_

#include "pc_test_uut.h"
#include "lwip/udp.h"
#include "lwip/tcpip.h"
#include "FreeRTOS.h"

/* ---------------------------------------------------------------------------
 * UDP send request
 * Allocated on the FreeRTOS heap by uut_send_result(), passed to
 * tcpip_callback(), and freed inside the lwIP core callback.
 * -------------------------------------------------------------------------*/
typedef struct {
	packet_t 	  res;  /* Test result to send to PC         */
    ip_addr_t     addr; /* Destination IP address (PC)       */
    u16_t         port; /* Destination port (PC source port) */
} udp_send_req_t;

/* ---------------------------------------------------------------------------
 * @brief  Sends a test result to the PC via the lwIP core task.
 *
 * Safe to call from any FreeRTOS task. Allocates a udp_send_req_t on the
 * heap, populates it, and schedules send_result_callback to run inside
 * defaultTask (the lwIP owner) via tcpip_callback().
 *
 * The caller must NOT free the request — it is freed inside the callback.
 *
 * @param  test_id   Test identifier echoed back to the PC.
 * @param  result    TEST_SUCCESS or TEST_FAILURE.
 * @param  pc_addr   Destination IP address (typically parsed from PC_IP).
 * @param  pc_port   Destination port (g_pc_port saved from the UDP packet).
 * @retval None
 * -------------------------------------------------------------------------*/
void uut_send_result(uint8_t opcode, uint16_t length, uint8_t *data,
                     const ip_addr_t *pc_addr, uint16_t pc_port);

#endif /* INC_UUT_TASK_H_ */
