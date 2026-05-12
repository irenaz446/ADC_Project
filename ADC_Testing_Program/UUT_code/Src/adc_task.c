#include "adc.h"
#include "uut_task.h"
#include "queue.h"

#define ADC_BUF_LEN 64

static uint16_t adc_dma_buffer[ADC_BUF_LEN];

extern TaskHandle_t  adcTaskHandle;
extern QueueHandle_t adcQueueHandle;
extern ip_addr_t     g_pc_ip_addr;
extern uint16_t      g_pc_port;

void StartADCTask(void *argument)
{
    packet_t rx_cmd;

    for (;;) {
        /* Wait for a command from the UDP receive callback */
        if (xQueueReceive(adcQueueHandle, &rx_cmd, portMAX_DELAY) != pdPASS) {
            continue;
        }

        /* Reset ADC/DMA state before starting a new acquisition */
        HAL_ADC_Stop_DMA(&hadc1);
        __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR | ADC_FLAG_EOC);
        hadc1.ErrorCode = HAL_ADC_ERROR_NONE;
        ulTaskNotifyTake(pdTRUE, 0); /* Flush any stale notification */

        /* Start DMA transfer — HAL_ADC_ConvCpltCallback will notify us when done */
        HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buffer, ADC_BUF_LEN);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Average the samples */
        uint32_t sum = 0;
        for (int i = 0; i < ADC_BUF_LEN; i++) {
            sum += adc_dma_buffer[i];
        }
        uint16_t avg = (uint16_t)(sum / ADC_BUF_LEN);

        uut_send_result(ADC_TEST, sizeof(avg), (uint8_t *)&avg, &g_pc_ip_addr, g_pc_port);
    }
}

/**
 * @brief  Called by HAL when DMA has filled the entire buffer.
 *         Notifies StartADCTask to process the samples.
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) {
        return;
    }
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(adcTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief  Called by HAL on ADC/DMA error. Logs the error code.
 */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        printf("ADC error code: %lu\r\n", HAL_ADC_GetError(hadc));
    }
}
