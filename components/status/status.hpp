#pragma once

#include "logger.hpp"
#include "gpio.hpp"

namespace status
{
    static const char *TAG = "status";

    class Controller
    {
    private:
        logger::Logger *logger;
        gpio::Analog *redPin;
        gpio::Analog *greenPin;
        gpio::Analog *bluePin;

    public:
        static Controller *New(logger::Logger *logger);
    };
}