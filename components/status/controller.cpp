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

        controller->redPin = gpio::Analog::New(GPIO_NUM_5, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);
        controller->greenPin = gpio::Analog::New(GPIO_NUM_6, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);
        controller->bluePin = gpio::Analog::New(GPIO_NUM_4, GPIO_MODE_OUTPUT, LEDC_TIMER_0, LEDC_TIMER_8_BIT, 1000);

        controller->redPin->SetLevel(0);
        controller->greenPin->SetLevel(255);
        controller->bluePin->SetLevel(0);

        return controller;
    }
}