#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "logger.hpp"
#include "gpio.hpp"
#include "status.hpp"

namespace status
{
    Controller *Controller::New(logger::Logger *logger)
    {
        Controller *controller = new Controller();

        controller->logger = logger;

        controller->redPin = gpio::Analog::New(GPIO_NUM_14, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);
        controller->greenPin = gpio::Analog::New(GPIO_NUM_13, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);
        controller->bluePin = gpio::Analog::New(GPIO_NUM_12, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);

        return controller;
    }
}