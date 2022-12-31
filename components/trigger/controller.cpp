#include <string.h>
#include "esp_err.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ccronexpr.h"
#include "logger.hpp"
#include "chron.hpp"
#include "device.hpp"
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

    Controller *Controller::New(logger::Logger *logger, chron::Controller *chron, device::Transmitter *transmitter,
                                device::Controller *device, database::Database *database)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Controller();

        // Inject dependencies
        Instance->logger = logger;
        Instance->chron = chron;
        Instance->transmitter = transmitter;
        Instance->device = device;
        Instance->db = database->Open(DB_NAMESPACE);

        // Create trigger scheduler task
        xTaskCreatePinnedToCore(Instance->taskFunc, "Trigger", 4 * 1024, NULL, 7, &Instance->taskHandle, tskNO_AFFINITY);

        return Instance;
    }

    const char *Controller::fixSchedule(const char *schedule)
    {
        // Match up to 1 minute fixing 59 on seconds
        return strcat(strcpy((char *)malloc(strlen(schedule) + 3 + 1), "59 "), schedule);
    }

    void Controller::taskFunc(void *args)
    {
        time_t now;
        time_t then;
        cron_expr schedule;
        const char *err;
        const char *expr;

        while (1)
        {
            // Get current time
            now = Instance->chron->Now();

            // Get all triggers
            uint32_t size;
            Trigger *triggers = Instance->List(&size);

            // Test schedule of all triggers
            for (int i = 0; i < size; i++)
            {
                memset(&schedule, 0, sizeof(cron_expr));
                err = NULL;
                expr = NULL;

                // Fix schedule seconds
                expr = Instance->fixSchedule(triggers[i].Schedule);

                // Parse trigger schedule
                cron_parse_expr(expr, &schedule, &err);
                if (err != NULL)
                    ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
                free((void *)expr);

                // Match up to 1 minute ignoring seconds and ensure we are not in the future
                then = cron_next(&schedule, now) - now;
                if (then >= 0 && then <= 59)
                {
                    // Get actuator to trigger
                    device::Device *actuator = Instance->device->GetByName(triggers[i].Actuator);
                    if (actuator == NULL)
                        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);

                    device::Packet command = device::Packet();
                    command.Protocol = actuator->Protocol;

                    // Make packet depending on actuator subtype
                    if (!strcmp(actuator->Subtype, device::Subtypes::Button))
                        strcpy(command.Data, actuator->Context.Button.Command);

                    // Send command to actuator
                    Instance->transmitter->Send(&command);

                    Instance->logger->Debug(TAG, "Actuator %s triggered by %s", actuator->Name, triggers[i].Name);

                    delete actuator;
                }
            }

            delete[] triggers;

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

    bool Controller::IsScheduleValid(const char *schedule)
    {
        cron_expr ign;
        const char *err;
        memset(&ign, 0, sizeof(cron_expr));
        const char *expr = this->fixSchedule(schedule);

        cron_parse_expr(expr, &ign, &err);
        free((void *)expr);

        return err == NULL;
    }
}