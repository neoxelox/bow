#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"

namespace gpio
{
    static const char *TAG = "gpio";

    static const uint32_t LOW = 0;
    static const uint32_t HIGH = 1;

    class Base
    {
    public:
        gpio_num_t Pin;
        gpio_mode_t Mode;

    protected:
        void New(gpio_num_t pin, gpio_mode_t mode);
    };

    class Digital : public Base
    {
    public:
        static Digital *New(gpio_num_t pin, gpio_mode_t mode);

    public:
        void SetLevel(uint32_t level);
        uint32_t GetLevel();
        void AttachPullResistor(gpio_pull_mode_t pull);
        void DetachPullResistor();
        void AttachInterrupt(gpio_int_type_t type, gpio_isr_t handler, void *args);
        void DetachInterrupt();
    };

    class Analog : public Base
    {
    private:
        inline static uint8_t channelCount;

    public:
        ledc_channel_t Channel;

    public:
        static Analog *New(gpio_num_t pin, gpio_mode_t mode, ledc_timer_t timer, ledc_timer_bit_t res, uint32_t freq);

    public:
        void SetLevel(uint32_t level);
    };
}