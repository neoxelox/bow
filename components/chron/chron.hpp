#pragma once

#include "i2cdev.h"
#include "logger.hpp"

namespace chron
{
    static const char *TAG = "chron";

    class Controller
    {
    private:
        logger::Logger *logger;
        i2c_dev_t dev;

    public:
        static Controller *New(logger::Logger *logger);
    };
}