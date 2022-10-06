#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "gpio.hpp"

namespace gpio
{
    Gpio::Gpio(gpio_num_t pin, gpio_mode_t mode) : Pin{pin}, Mode{mode}
    {
        ESP_ERROR_CHECK(!GPIO_IS_VALID_GPIO(this->Pin));

        if (this->Mode >= GPIO_MODE_OUTPUT)
            ESP_ERROR_CHECK(!GPIO_IS_VALID_OUTPUT_GPIO(this->Pin));

        ESP_ERROR_CHECK(gpio_reset_pin(this->Pin));

        const gpio_config_t cfg = {
            .pin_bit_mask = 1ull << this->Pin,
            .mode = this->Mode,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_ERROR_CHECK(gpio_config(&cfg));
    }

    void Gpio::DigitalWrite(int level)
    {
        ESP_ERROR_CHECK(gpio_set_level(this->Pin, level));
    }

    int Gpio::DigitalRead()
    {
        return gpio_get_level(this->Pin);
    }

    void Gpio::AttachPullResistor(gpio_pull_mode_t pull)
    {
        ESP_ERROR_CHECK(gpio_set_pull_mode(this->Pin, pull));
    }

    void Gpio::DetachPullResistor()
    {
        ESP_ERROR_CHECK(gpio_set_pull_mode(this->Pin, GPIO_FLOATING));
    }

    void Gpio::AttachInterrupt(gpio_int_type_t type, gpio_isr_t handler, void *args)
    {
        esp_err_t ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
            ESP_ERROR_CHECK(ret);

        ESP_ERROR_CHECK(gpio_set_intr_type(this->Pin, type));
        ESP_ERROR_CHECK(gpio_isr_handler_add(this->Pin, handler, args));
        ESP_ERROR_CHECK(gpio_intr_enable(this->Pin));
    }

    void Gpio::DetachInterrupt()
    {
        ESP_ERROR_CHECK(gpio_intr_disable(this->Pin));
        ESP_ERROR_CHECK(gpio_isr_handler_remove(this->Pin));
        ESP_ERROR_CHECK(gpio_set_intr_type(this->Pin, GPIO_INTR_DISABLE));
    }
}