#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Define button GPIOs
#define GPIO_KEY1     11
#define GPIO_KEY2     0
#define GPIO_KEY3     39

// Define input bitmask
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_KEY1) | (1ULL<<GPIO_KEY2) | (1ULL<<GPIO_KEY3))

void button_init(void);

#endif // BUTTON_H
