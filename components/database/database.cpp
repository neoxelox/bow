#include "esp_err.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"

namespace database
{
    Database *Database::New(logger::Logger *logger)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Database();

        // Inject dependencies
        Instance->logger = logger;

        // Initialize database NVS partition
        esp_err_t err = nvs_flash_init_partition(PARTITION);
        // Database NVS partition has been truncated or format cannot be recognized
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase_partition(PARTITION));
            err = nvs_flash_init_partition(PARTITION);
        }
        ESP_ERROR_CHECK(err);

        nvs_stats_t info;
        Instance->Info(&info);
        Instance->logger->Debug(TAG, "NVS entries: Used %d | Free %d | Total %d",
                                info.used_entries, info.free_entries, info.total_entries);
        Instance->logger->Debug(TAG, "NVS namespaces: Total %d", info.namespace_count);

        return Instance;
    }

    void Database::Info(nvs_stats_t *info)
    {
        ESP_ERROR_CHECK(nvs_get_stats(PARTITION, info));
    }

    Handle *Database::Open(const char *nmspace)
    {
        Handle *handle = new Handle();

        handle->nmspace = nmspace;

        ESP_ERROR_CHECK(nvs_open_from_partition(PARTITION, handle->nmspace, NVS_READWRITE, &handle->handle));

        return handle;
    }

    esp_err_t Handle::Drop()
    {
        esp_err_t err;

        err = nvs_erase_all(this->handle);
        if (err != ESP_OK)
            return err;

        err = nvs_commit(this->handle);
        if (err != ESP_OK)
            return err;

        return ESP_OK;
    }

    esp_err_t Handle::Count(uint32_t *count)
    {
        esp_err_t err;

        err = nvs_get_used_entry_count(this->handle, (size_t *)count);
        if (err != ESP_OK)
            return err;

        // The namespace name takes one entry
        (*count)++;

        return ESP_OK;
    }

    esp_err_t Handle::Get(const char *key, cJSON **value)
    {
        esp_err_t err;

        uint32_t size;

        err = nvs_get_str(this->handle, key, NULL, (size_t *)&size);
        if (err == ESP_ERR_NVS_NOT_FOUND)
            return ESP_OK;
        else if (err != ESP_OK)
            return err;

        char *item = (char *)malloc(size);

        err = nvs_get_str(this->handle, key, item, (size_t *)&size);
        if (err != ESP_OK)
        {
            free((void *)item);

            if (err == ESP_ERR_NVS_NOT_FOUND)
                return ESP_OK;
            return err;
        }

        *value = cJSON_Parse(item);
        free((void *)item);

        return ESP_OK;
    }

    esp_err_t Handle::Set(const char *key, cJSON *value)
    {
        esp_err_t err;

        const char *item = cJSON_PrintUnformatted(value);

        err = nvs_set_str(this->handle, key, item);
        if (err != ESP_OK)
        {
            free((void *)item);
            return err;
        }

        err = nvs_commit(this->handle);
        if (err != ESP_OK)
        {
            free((void *)item);
            return err;
        }

        free((void *)item);

        return ESP_OK;
    }

    esp_err_t Handle::Find(db_find_cb_t find, void *context)
    {
        esp_err_t err;

        nvs_entry_info_t itemInfo;
        nvs_iterator_t iter = NULL;

        err = nvs_entry_find(PARTITION, this->nmspace, NVS_TYPE_STR, &iter);
        if (err == ESP_ERR_NVS_NOT_FOUND)
            return ESP_OK;
        else if (err != ESP_OK)
            return err;

        while (err == ESP_OK)
        {
            nvs_entry_info(iter, &itemInfo);
            if (find(itemInfo.key, context))
                break;
            err = nvs_entry_next(&iter);
        }
        nvs_release_iterator(iter);

        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            return err;

        return ESP_OK;
    }

    esp_err_t Handle::Delete(const char *key)
    {
        esp_err_t err;

        err = nvs_erase_key(this->handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND)
            return ESP_OK;
        else if (err != ESP_OK)
            return err;

        err = nvs_commit(this->handle);
        if (err != ESP_OK)
            return err;

        return ESP_OK;
    }
}