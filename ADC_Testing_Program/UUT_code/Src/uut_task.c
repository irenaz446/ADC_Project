/**
 * @file    uut_task.c
 * @brief   Shared utilities for all UUT peripheral test tasks.
 *
 * Provides:
 *   UUT_ComputeCRC   — zlib-compatible CRC32 using the STM32 hardware CRC unit.
 *   uut_send_result  — thread-safe UDP result sender via tcpip_callback.
 */

#include "uut_task.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern CRC_HandleTypeDef hcrc;

/**
 * @brief  Calculates a zlib-compatible CRC32 over a byte buffer.
 * @param  data  Pointer to the data buffer.
 * @param  len   Length in bytes.
 * @retval 32-bit CRC result.
 */
uint32_t UUT_ComputeCRC(uint8_t *data, uint32_t len)
{
    __HAL_CRC_DR_RESET(&hcrc);
    uint32_t crc = HAL_CRC_Accumulate(&hcrc, (uint32_t *)data, len);
    return (crc ^ 0xFFFFFFFF); /* Final XOR to match zlib convention */
}

/**
 * @brief  lwIP core callback that performs the actual UDP send.
 *
 * Invoked by tcpip_callback() inside defaultTask (the lwIP owner), so all
 * lwIP API calls here are safe. Frees the udp_send_req_t when done.
 *
 * @param  arg  Heap-allocated udp_send_req_t populated by uut_send_result().
 */
static void send_result_callback(void *arg)
{
    udp_send_req_t *req = (udp_send_req_t *)arg;

    /* Total wire size: Opcode(1) + Length(2) + Data(n) + CRC(4) */
    uint16_t send_size = 1 + 2 + req->res.length + 4;

    struct udp_pcb *pcb = udp_new();
    if (pcb != NULL) {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, send_size, PBUF_RAM);
        if (p != NULL) {
            memcpy(p->payload, &req->res, send_size);
            udp_sendto(pcb, p, &req->addr, req->port);
            pbuf_free(p);
        }
        udp_remove(pcb);
    }

    vPortFree(req);
}

/**
 * @brief  Sends a test result to the PC via the lwIP core task.
 *
 * Safe to call from any FreeRTOS task. Allocates a udp_send_req_t on the
 * heap, builds the packet, and schedules send_result_callback via
 * tcpip_callback() so the actual send happens inside the lwIP owner task.
 *
 * @param  opcode    Test identifier echoed back to the PC.
 * @param  length    Byte length of data.
 * @param  data      Pointer to the payload bytes.
 * @param  pc_addr   Destination IP address.
 * @param  pc_port   Destination port (captured from the incoming UDP packet).
 */
void uut_send_result(uint8_t opcode, uint16_t length, uint8_t *data,
                     const ip_addr_t *pc_addr, uint16_t pc_port)
{
    udp_send_req_t *req = pvPortMalloc(sizeof(udp_send_req_t));
    if (req == NULL) {
        printf("ERROR: uut_send_result — heap allocation failed\r\n");
        return;
    }

    /* Build packet header */
    req->res.opcode = opcode;
    req->res.length = length;
    memcpy(req->res.data, data, length);

    /* Append CRC over Opcode + Length + Data */
    uint32_t payload_len = 1 + 2 + length;
    uint32_t tx_crc = UUT_ComputeCRC((uint8_t *)&req->res, payload_len);
    memcpy(&req->res.data[length], &tx_crc, 4);

    /* Set destination */
    req->addr = *pc_addr;
    req->port = pc_port;

    printf("Send result: opcode=%d, length=%d\r\n", opcode, length);

    tcpip_callback(send_result_callback, req);
}
