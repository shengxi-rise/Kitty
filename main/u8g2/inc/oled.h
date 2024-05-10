//
// Created by 19693 on 2024/1/20.
//

#ifndef OLED_OLED_H
#define OLED_OLED_H

#include "u8g2.h"
#include "u8x8.h"

uint8_t u8x8_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
void draw(u8g2_t *u8g2,const char *buf,const uint8_t* image);
void time_display(u8g2_t *u8g2,const char* buf);

#endif //OLED_OLED_H
