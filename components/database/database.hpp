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

    class Database
    {
    private:
        logger::Logger *logger;

    public:
        inline static Database *Instance;
        static Database *New(logger::Logger *logger);

    public:
        void Info(nvs_stats_t *info);
        esp_err_t Open(nvs_handle_t *handle, const char *nmspace);
        esp_err_t Drop(nvs_handle_t handle);
        esp_err_t Count(nvs_handle_t handle, uint32_t *count);
        esp_err_t Get(nvs_handle_t handle, const char *key, cJSON **value);
        esp_err_t Set(nvs_handle_t handle, const char *key, cJSON *value);
        esp_err_t Find(const char *nmspace, db_find_cb_t find, void *context);
        esp_err_t Delete(nvs_handle_t handle, const char *key);
    };
}