#include "pc_test_uut.h"
#include "uut_task.h"
#include "adc.h"
#include "crc.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

extern packet_t g_adc_cmd;
extern SemaphoreHandle_t adcSemHandle;
extern ip_addr_t g_pc_ip_addr;
extern uint16_t  g_pc_port;

void StartADCTask(void *argument) {
    uint32_t sum;
    uint16_t avg;
    const int samples = 64;

    for(;;) {
        // 1. Wait for the Dispatcher to trigger the test
        if (xSemaphoreTake(adcSemHandle, portMAX_DELAY) == pdPASS) {
            sum = 0;

            // 2. Perform the measurement on the 3.3V pin
            for(int i = 0; i < samples; i++) {
                HAL_ADC_Start(&hadc1);
                if(HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
                    sum += HAL_ADC_GetValue(&hadc1);
                }
                HAL_ADC_Stop(&hadc1);
            }
            avg = (uint16_t)(sum / samples);

            // 3. Send the result using the high-level API
            // We pass the Opcode (16), Length (2 bytes), and the data itself.
            // The function handles the CRC and UDP addressing internally.
            uut_send_result(ADC_TEST, 2, (uint8_t*)&avg, &g_pc_ip_addr, g_pc_port);
        }
    }
}
