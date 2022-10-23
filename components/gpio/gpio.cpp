#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_intr_alloc.h"
#include "gpio.hpp"

namespace gpio
{
    void Base::New(gpio_num_t pin, gpio_mode_t mode)
    {
        ESP_ERROR_CHECK(!GPIO_IS_VALID_GPIO(pin));

        if (mode >= GPIO_MODE_OUTPUT)
            ESP_ERROR_CHECK(!GPIO_IS_VALID_OUTPUT_GPIO(pin));

        ESP_ERROR_CHECK(gpio_reset_pin(pin));

        this->Pin = pin;
        this->Mode = mode;
    }

    Digital *Digital::New(gpio_num_t pin, gpio_mode_t mode)
    {
        Digital *gpio = new Digital();
        gpio->Base::New(pin, mode);

        const gpio_config_t cfg = {
            .pin_bit_mask = 1ull << gpio->Pin,
            .mode = gpio->Mode,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_ERROR_CHECK(gpio_config(&cfg));

        return gpio;
    }

    void Digital::SetLevel(uint32_t level)
    {
        ESP_ERROR_CHECK(gpio_set_level(this->Pin, level));
    }

    uint32_t Digital::GetLevel()
    {
        return gpio_get_level(this->Pin);
    }

    void Digital::AttachPullResistor(gpio_pull_mode_t pull)
    {
        ESP_ERROR_CHECK(gpio_set_pull_mode(this->Pin, pull));
    }

    void Digital::DetachPullResistor()
    {
        ESP_ERROR_CHECK(gpio_set_pull_mode(this->Pin, GPIO_FLOATING));
    }

    void Digital::AttachInterrupt(gpio_int_type_t type, gpio_isr_t handler, void *args)
    {
        esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
            ESP_ERROR_CHECK(ret);

        ESP_ERROR_CHECK(gpio_set_intr_type(this->Pin, type));
        ESP_ERROR_CHECK(gpio_isr_handler_add(this->Pin, handler, args));
        ESP_ERROR_CHECK(gpio_intr_enable(this->Pin));
    }

    void Digital::DetachInterrupt()
    {
        ESP_ERROR_CHECK(gpio_intr_disable(this->Pin));
        ESP_ERROR_CHECK(gpio_isr_handler_remove(this->Pin));
        ESP_ERROR_CHECK(gpio_set_intr_type(this->Pin, GPIO_INTR_DISABLE));
    }

    Analog *Analog::New(gpio_num_t pin, gpio_mode_t mode, ledc_timer_t timer, ledc_timer_bit_t res, uint32_t freq)
    {
        // Only accept analog output for now
        if (mode < GPIO_MODE_OUTPUT)
            ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);

        Analog *gpio = new Analog();
        gpio->Base::New(pin, mode);
        gpio->Channel = (ledc_channel_t)Analog::channelCount++;

        if (gpio->Channel >= LEDC_CHANNEL_MAX)
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);

        const ledc_timer_config_t timerCfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE, // ESP32-S3 can only use low speed mode
            .duty_resolution = res,
            .timer_num = timer,
            .freq_hz = freq,
            .clk_cfg = LEDC_AUTO_CLK,
        };

        ESP_ERROR_CHECK(ledc_timer_config(&timerCfg));

        const ledc_channel_config_t channelCfg = {
            .gpio_num = gpio->Pin,
            .speed_mode = LEDC_LOW_SPEED_MODE, // ESP32-S3 can only use low speed mode
            .channel = gpio->Channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = timer,
            .duty = 0,
            .hpoint = 0,
        };

        ESP_ERROR_CHECK(ledc_channel_config(&channelCfg));

        return gpio;
    }

    void Analog::SetLevel(uint32_t level)
    {
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, this->Channel, level));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, this->Channel));
    }
}