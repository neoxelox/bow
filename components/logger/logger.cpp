#include <stdio.h>
#include <stdarg.h>
#include "logger.hpp"
#include "esp_log.h"

#define VPRINTF_STACK_BUFFER_SIZE 80

namespace logger
{
    Logger::Logger(esp_log_level_t level)
    {
        this->SetLevel(level);
    }

    Logger::~Logger() {}

    esp_log_level_t Logger::GetLevel() const
    {
        return esp_log_level_get("*");
    }

    void Logger::SetLevel(esp_log_level_t level)
    {
        this->level = level;
        esp_log_level_set("*", level);
    }

    void Logger::Debug(const char *tag, const char *format, ...)
    {
        if (this->level < ESP_LOG_DEBUG)
            return;

        va_list args;
        va_start(args, format);

        this->espLog(ESP_LOG_DEBUG, tag, format, args);

        va_end(args);
    }

    void Logger::Info(const char *tag, const char *format, ...)
    {
        if (this->level < ESP_LOG_INFO)
            return;

        va_list args;
        va_start(args, format);

        this->espLog(ESP_LOG_INFO, tag, format, args);

        va_end(args);
    }

    void Logger::Warn(const char *tag, const char *format, ...)
    {
        if (this->level < ESP_LOG_WARN)
            return;

        va_list args;
        va_start(args, format);

        this->espLog(ESP_LOG_WARN, tag, format, args);

        va_end(args);
    }

    void Logger::Error(const char *tag, const char *format, ...)
    {
        if (this->level < ESP_LOG_ERROR)
            return;

        va_list args;
        va_start(args, format);

        this->espLog(ESP_LOG_ERROR, tag, format, args);

        va_end(args);
    }

    void Logger::Level(esp_log_level_t level, const char *tag, const char *format, ...)
    {
        if (this->level < level)
            return;

        va_list args;
        va_start(args, format);

        this->espLog(level, tag, format, args);

        va_end(args);
    }

    int Logger::espLog(esp_log_level_t level, const char *tag, const char *format, va_list args)
    {
        char buff[VPRINTF_STACK_BUFFER_SIZE];
        int len = vsnprintf(buff, sizeof(buff) - 1, format, args);
        buff[sizeof(buff) - 1] = 0;

        int i;
        for (i = len - 1; i >= 0; --i)
        {
            if (buff[i] != '\n' && buff[i] != '\r' && buff[i] != ' ')
                break;
            buff[i] = 0;
        }

        if (i > 0)
            ESP_LOG_LEVEL(level, tag, "%s", buff);

        return len;
    }
}