#include <string.h>
#include "esp_err.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ccronexpr.h"
#include "logger.hpp"
#include "chron.hpp"
#include "database.hpp"
#include "trigger.hpp"

namespace trigger
{
    Trigger::Trigger()
    {
        this->Name = NULL;
        this->Actuator = NULL;
        this->Schedule = NULL;
        this->Emoji = NULL;
        this->Creator = NULL;
        this->CreatedAt = 0;
    }

    Trigger::Trigger(const char *name, const char *actuator, const char *schedule,
                     const char *emoji, const char *creator, time_t createdAt)
    {
        this->Name = strdup(name);
        this->Actuator = strdup(actuator);
        this->Schedule = strdup(schedule);
        this->Emoji = strdup(emoji);
        this->Creator = strdup(creator);
        this->CreatedAt = createdAt;
    }

    Trigger::Trigger(cJSON *src)
    {
        this->Name = strdup(cJSON_GetObjectItem(src, "name")->valuestring);
        this->Actuator = strdup(cJSON_GetObjectItem(src, "actuator")->valuestring);
        this->Schedule = strdup(cJSON_GetObjectItem(src, "schedule")->valuestring);
        this->Emoji = strdup(cJSON_GetObjectItem(src, "emoji")->valuestring);
        this->Creator = strdup(cJSON_GetObjectItem(src, "creator")->valuestring);
        this->CreatedAt = cJSON_GetObjectItem(src, "created_at")->valueint;
    }

    Trigger::~Trigger()
    {
        free((void *)this->Name);
        free((void *)this->Actuator);
        free((void *)this->Schedule);
        free((void *)this->Emoji);
        free((void *)this->Creator);
    }

    Trigger &Trigger::operator=(const Trigger &other)
    {
        if (this == &other)
            return *this;

        free((void *)this->Name);
        free((void *)this->Actuator);
        free((void *)this->Schedule);
        free((void *)this->Emoji);
        free((void *)this->Creator);

        this->Name = strdup(other.Name);
        this->Actuator = strdup(other.Actuator);
        this->Schedule = strdup(other.Schedule);
        this->Emoji = strdup(other.Emoji);
        this->Creator = strdup(other.Creator);
        this->CreatedAt = other.CreatedAt;

        return *this;
    }

    cJSON *Trigger::JSON()
    {
        cJSON *root = cJSON_CreateObject();

        cJSON_AddStringToObject(root, "name", this->Name);
        cJSON_AddStringToObject(root, "actuator", this->Actuator);
        cJSON_AddStringToObject(root, "schedule", this->Schedule);
        cJSON_AddStringToObject(root, "emoji", this->Emoji);
        cJSON_AddStringToObject(root, "creator", this->Creator);
        cJSON_AddNumberToObject(root, "created_at", this->CreatedAt);

        return root;
    }

    Controller *Controller::New(logger::Logger *logger, chron::Controller *chron, database::Database *database)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Controller();

        // Inject dependencies
        Instance->logger = logger;
        Instance->chron = chron;
        Instance->db = database->Open(DB_NAMESPACE);

        // Create trigger scheduler task
        xTaskCreatePinnedToCore(Instance->taskFunc, "Trigger", 4 * 1024, NULL, 7, &Instance->taskHandle, tskNO_AFFINITY);

        return Instance;
    }

    void Controller::taskFunc(void *args)
    {
        while (1)
        {
            Instance->logger->Debug(TAG, "Scheduling...");

            vTaskDelay(SCHEDULER_PERIOD);
        }
    }

    uint32_t Controller::Count()
    {
        uint32_t count;

        ESP_ERROR_CHECK(this->db->Count(&count));

        return count;
    }

    Trigger *Controller::Get(const char *name)
    {
        Trigger *trigger = NULL;

        cJSON *triggerJSON = NULL;
        ESP_ERROR_CHECK(this->db->Get(name, &triggerJSON));

        if (triggerJSON != NULL)
            trigger = new Trigger(triggerJSON);

        cJSON_Delete(triggerJSON);

        return trigger;
    }

    Trigger *Controller::List(uint32_t *size)
    {
        uint32_t count = this->Count();
        *size = count;
        if (count < 1)
            return NULL;

        Trigger *list = new Trigger[count];
        Trigger *aux = list;

        ESP_ERROR_CHECK(this->db->Find(
            [](const char *key, void *context) -> bool
            {
                Trigger *list = *(Trigger **)context;

                cJSON *triggerJSON = NULL;
                ESP_ERROR_CHECK(Instance->db->Get(key, &triggerJSON));

                *list = Trigger(triggerJSON);
                (*(Trigger **)context)++;

                cJSON_Delete(triggerJSON);
                return false;
            },
            &aux));

        return list;
    }

    void Controller::Set(Trigger *trigger)
    {
        cJSON *triggerJSON = trigger->JSON();
        ESP_ERROR_CHECK(this->db->Set(trigger->Name, triggerJSON));
        cJSON_Delete(triggerJSON);
    }

    void Controller::Delete(const char *name)
    {
        ESP_ERROR_CHECK(this->db->Delete(name));
    }

    void Controller::Drop()
    {
        ESP_ERROR_CHECK(this->db->Drop());
    }
}