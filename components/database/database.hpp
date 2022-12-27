#pragma once

#include "esp_err.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "logger.hpp"

namespace database
{
    static const char *TAG = "database";

    static const char *PARTITION = "database";

    typedef bool (*db_find_cb_t)(const char *key, void *context);

    // Database class forward declaration
    class Database;

    class Handle
    {
    private:
        nvs_handle_t handle;
        const char *nmspace;

    public:
        esp_err_t Drop();
        esp_err_t Count(uint32_t *count);
        esp_err_t Get(const char *key, cJSON **value);
        esp_err_t Set(const char *key, cJSON *value);
        esp_err_t Find(db_find_cb_t find, void *context);
        esp_err_t Delete(const char *key);

        friend class Database;
    };

    class Database
    {
    private:
        logger::Logger *logger;

    public:
        inline static Database *Instance;
        static Database *New(logger::Logger *logger);

    public:
        void Info(nvs_stats_t *info);
        Handle *Open(const char *nmspace);
        void Reset();
    };
}