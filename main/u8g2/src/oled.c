//
// Created by 19693 on 2024/1/24.
//
#include "stdio.h"
#include "../u8g2/inc/u8x8.h"
#include "gpio.h"
#include "freertos/FreeRTOS.h"

#include "task.h"
#include "inc/oled.h"

#define I2C_SCL_IO           14                /*!< gpio number for I2C master clock */
#define I2C_SDA_IO           2

static __inline void
delay_clock(int ts) {
    uint32_t start, curr;

    __asm__ __volatile__("rsr %0, ccount" : "=r"(start));
    do
        __asm__ __volatile__("rsr %0, ccount" : "=r"(curr));
    while (curr - start <= ts);
}

#define delay_us(val)       delay_clock(240*val)
#define delay_100ns(val)    delay_clock(24*val)

uint8_t u8x8_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_DELAY_100NANO: // delay arg_int * 100 nano seconds
            delay_100ns(arg_int);
            break;
        case U8X8_MSG_DELAY_10MICRO: // delay arg_int * 10 micro seconds
            delay_us(arg_int * 10);
            break;
        case U8X8_MSG_DELAY_MILLI: // delay arg_int * 1 milli second
            vTaskDelay(1000 / portTICK_RATE_MS); //在这里既然使用了freertos的vTaskDelay，那么u8g2必须在进程上下文运行
            break;
        case U8X8_MSG_DELAY_I2C: // arg_int is the I2C speed in 100KHz, e.g. 4 = 400 KHz
            delay_us(10 / arg_int);
            break;                    // arg_int=1: delay by 5us, arg_int = 4: delay by 1.25us
        case U8X8_MSG_GPIO_I2C_CLOCK: // arg_int=0: Output low at I2C clock pin
            if (arg_int == 1) {
                gpio_set_level(I2C_SCL_IO, 1);
            } else if (arg_int == 0) {
                gpio_set_level(I2C_SCL_IO, 0);
            }
            break;                    // arg_int=1: Input dir with pullup high for I2C clock pin
        case U8X8_MSG_GPIO_I2C_DATA:  // arg_int=0: Output low at I2C data pin
            if (arg_int == 1) {
                gpio_set_level(I2C_SDA_IO, 1);
            } else if (arg_int == 0) {
                gpio_set_level(I2C_SDA_IO, 0);
            }
            break;                    // arg_int=1: Input dir with pullup high for I2C data pin
        case U8X8_MSG_GPIO_MENU_SELECT:
            u8x8_SetGPIOResult(u8x8, /* get menu select pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_NEXT:
            u8x8_SetGPIOResult(u8x8, /* get menu next pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_PREV:
            u8x8_SetGPIOResult(u8x8, /* get menu prev pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_HOME:
            u8x8_SetGPIOResult(u8x8, /* get menu home pin state */ 0);
            break;
        default:
            u8x8_SetGPIOResult(u8x8, 1); // default return value
            break;
    }
    return 1;
}

void draw(u8g2_t *u8g2, const char *buf,const uint8_t *image) {
//    u8g2_SetFontMode(u8g2, 1); /*字体模式选择*/
//    u8g2_SetFontDirection(u8g2, 0); /*字体方向选择*/
    u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312); /*字库选择*/

    u8g2_DrawXBMP(u8g2, 10, 30, 32, 25, image);
    u8g2_DrawUTF8(u8g2, 45, 50, buf);
    u8g2_SendBuffer(u8g2);

}

void time_display(u8g2_t *u8g2,const char* buf) {
    u8g2_SetFont(u8g2, u8g2_font_inb16_mn);
    u8g2_DrawStr(u8g2, 5, 20, buf);
    u8g2_SendBuffer(u8g2);
}