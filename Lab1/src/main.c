#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include <string.h>

#define UART_TXD (GPIO_NUM_1)
#define UART_RXD (GPIO_NUM_3)
#define BLINK_GPIO 2

void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_set_pin(UART_NUM_0, UART_TXD, UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
}

void app_main(void)
{
    uart_init();
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while (1)
    {
        gpio_set_level(BLINK_GPIO, 0);
        uart_write_bytes(UART_NUM_0, "OFF\n", strlen("OFF\n"));
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
        gpio_set_level(BLINK_GPIO, 1);
        uart_write_bytes(UART_NUM_0, "ON\n", strlen("ON\n"));
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
    }
}