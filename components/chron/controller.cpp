#include "esp_err.h"
#include "driver/gpio.h"
#include "i2cdev.h"
#include "ds3231.h"
#include "logger.hpp"
#include "chron.hpp"

namespace chron
{
    Controller *Controller::New(logger::Logger *logger)
    {
        Controller *controller = new Controller();

        controller->logger = logger;

        ESP_ERROR_CHECK(i2cdev_init());
        ESP_ERROR_CHECK(ds3231_init_desc(&controller->dev, I2C_NUM_0, GPIO_NUM_1, GPIO_NUM_2));

        ESP_ERROR_CHECK(ds3231_clear_oscillator_stop_flag(&controller->dev));
        ESP_ERROR_CHECK(ds3231_clear_alarm_flags(&controller->dev, DS3231_ALARM_BOTH));
        ESP_ERROR_CHECK(ds3231_disable_alarm_ints(&controller->dev, DS3231_ALARM_BOTH));
        ESP_ERROR_CHECK(ds3231_disable_32khz(&controller->dev));
        ESP_ERROR_CHECK(ds3231_disable_squarewave(&controller->dev));

        return controller;
    }
}