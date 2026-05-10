/**
 * @file    uut_task.c
 * @brief   Shared utilities for all UUT peripheral test tasks.
 *
 * Implements the common functionality used by every peripheral task:
 *
 *   uut_send_result  — thread-safe UDP result sender via tcpip_callback.
 *
 * Both functions were previously copy-pasted into every task file
 * (uart_task.c, spi_task.c, i2c_task.c, adc_task.c, timer_task.c).
 * Centralising them here eliminates the duplication and ensures any
 * fix or improvement applies to all tests at once.
 */

#include "uut_task.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * External CRC handle (defined by CubeMX in crc.c)
 * -------------------------------------------------------------------------*/
extern CRC_HandleTypeDef hcrc;

/* ---------------------------------------------------------------------------
 * Private: lwIP core callback that executes the actual UDP send.
 *
 * Called by tcpip_callback() inside defaultTask (the lwIP owner).
 * All lwIP API calls here are safe because we are in the lwIP task context.
 * Frees the udp_send_req_t allocated by uut_send_result() before returning.
 *
 * @param  arg  Pointer to a heap-allocated udp_send_req_t.
 * @retval None
 * -------------------------------------------------------------------------*/
static void send_result_callback(void *arg)
{
    udp_send_req_t *req = (udp_send_req_t *)arg;
    struct udp_pcb *pcb = udp_new();
    // Total = Opcode(1) + Length(2) + Data(req->res.length) + CRC(4)
    uint16_t send_size = 1 + 2 + req->res.length + 4;
    if (pcb != NULL) {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, send_size, PBUF_RAM);
        if (p != NULL) {
            memcpy(p->payload, &req->res, send_size);
            udp_sendto(pcb, p, &req->addr, req->port);
            pbuf_free(p);
        }
        udp_remove(pcb);
    }

    vPortFree(req); /* Always free — even if send failed */
}

/* ---------------------------------------------------------------------------
 * @brief  Sends a test result to the PC via the lwIP core task.
 *
 * Allocates a udp_send_req_t on the FreeRTOS heap, populates it with the
 * result and destination, then delegates the actual UDP send to
 * send_result_callback via tcpip_callback(). This is the correct pattern
 * for calling lwIP from any task other than defaultTask.
 *
 * @param  test_id   Test identifier echoed back to the PC.
 * @param  result    TEST_SUCCESS or TEST_FAILURE.
 * @param  pc_addr   Destination IP address.
 * @param  pc_port   Destination port (g_pc_port from the incoming packet).
 * @retval None
 * -------------------------------------------------------------------------*/
void uut_send_result(uint8_t opcode, uint16_t length, uint8_t *data,
                     const ip_addr_t *pc_addr, uint16_t pc_port)
{
    // 1. Calculate total size: Opcode(1) + Length(2) + Data(n) + CRC(4)
    uint32_t payload_len = 1 + 2 + length;

    // 2. Allocate memory for the request structure
    // Ensure your udp_send_req_t is large enough for the new packet_t
    udp_send_req_t *udp_result = pvPortMalloc(sizeof(udp_send_req_t));

    if (udp_result != NULL) {
        // 3. Populate the Header
    	udp_result->res.opcode = opcode;
    	udp_result->res.length = length;

        // 4. Copy the ADC data (or any other peripheral data)
        memcpy(udp_result->res.data, data, length);

        // 5. Calculate CRC over Opcode + Length + Data
        // We use payload_len (total bytes before the CRC footer)
        __HAL_CRC_DR_RESET(&hcrc);

		// Feed bytes one by one. This avoids alignment issues and
		// handles non-word-multiple lengths (like 3 or 5 bytes) correctly.
		uint8_t *ptr = (uint8_t*)&udp_result->res;
		for (uint32_t i = 0; i < payload_len; i++) {
			*(__IO uint8_t *)&hcrc.Instance->DR = ptr[i];
		}
		// APPLY THE FINAL XOR (Bitwise NOT)
		// This makes the STM32 result match the PC's zlib/Python CRC32
		uint32_t tx_crc = hcrc.Instance->DR ^ 0xFFFFFFFF;

        // 6. Append CRC to the end of the data buffer
        // We place the 4-byte CRC immediately after the 'data' field
        memcpy(&udp_result->res.data[length], &tx_crc, 4);

        // 7. Set Network Info
        udp_result->addr = *pc_addr;
        udp_result->port = pc_port;
        printf("\r\nSend result to PC: (data[0]=%d, data[1]=%d)\r\n",
        		udp_result->res.data[0], udp_result->res.data[1]);
        // 8. Schedule the LwIP callback
        tcpip_callback(send_result_callback, udp_result);
    } else {
        printf("ERROR: uut_send_result — heap allocation failed\r\n");
    }
}
