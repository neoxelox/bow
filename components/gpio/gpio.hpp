#pragma once

#include "driver/gpio.h"

namespace gpio
{
    static const char *TAG = "gpio";

    static const int LOW = 0;
    static const int HIGH = 1;

    class Gpio
    {
    public:
        const gpio_num_t Pin;
        const gpio_mode_t Mode;

    public:
        Gpio(gpio_num_t pin, gpio_mode_t mode);

        void DigitalWrite(int level);
        int DigitalRead();
        void AttachPullResistor(gpio_pull_mode_t pull);
        void DetachPullResistor();
        void AttachInterrupt(gpio_int_type_t type, gpio_isr_t handler, void *args);
        void DetachInterrupt();
    };
}