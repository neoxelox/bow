#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "logger.hpp"
#include "database.hpp"
#include "device.hpp"
#include "status.hpp"
#include "chron.hpp"
#include "provisioner.hpp"
#include "server.hpp"
#include "user.hpp"
#include "trigger.hpp"
#include "role.hpp"

// TODO: Only for debug, remove
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// ----------------------------

namespace main
{
    static const char *TAG = "main";

    class App
    {
    private:
        const esp_app_desc_t *description;
        logger::Logger *logger;
        database::Database *database;
        device::Receiver *receiver;
        device::Transmitter *transmitter;
        status::Controller *status;
        chron::Controller *chron;
        provisioner::Provisioner *provisioner;
        user::Controller *user;
        device::Controller *device;
        trigger::Controller *trigger;
        role::Controller *role;
        server::Server *server;

    public:
        inline static App *Instance;

        static App *New()
        {
            if (Instance != NULL)
                return Instance;

            Instance = new App();

            int64_t elapsed = esp_timer_get_time();

            // Inject dependencies
            Instance->description = esp_app_get_description();
            Instance->logger = logger::Logger::New((esp_log_level_t)ESP_LOG_DEBUG); // TODO: Only for debug, change to CONFIG_LOG_DEFAULT_LEVEL
            Instance->status = status::Controller::New(Instance->logger);
            Instance->database = database::Database::New(Instance->logger);
            Instance->provisioner = provisioner::Provisioner::New(Instance->logger, Instance->status, Instance->database);
            Instance->chron = chron::Controller::New(Instance->logger, Instance->provisioner);
            Instance->user = user::Controller::New(Instance->logger, Instance->database);
            Instance->device = device::Controller::New(Instance->logger, Instance->database);
            Instance->trigger = trigger::Controller::New(Instance->logger, Instance->chron, Instance->database);
            Instance->role = role::Controller::New(Instance->logger, Instance->database);
            Instance->receiver = device::Receiver::New(Instance->logger, Instance->status, Instance->device);
            Instance->transmitter = device::Transmitter::New(Instance->logger, Instance->status);
            Instance->server = server::Server::New(Instance->logger, Instance->database, Instance->provisioner,
                                                   Instance->chron, Instance->transmitter, Instance->receiver,
                                                   Instance->user, Instance->device, Instance->trigger,
                                                   Instance->role);

            elapsed = esp_timer_get_time() - elapsed;
            Instance->logger->Info(TAG, "Startup took %f ms", (float)(elapsed / 1000.0l));
            Instance->logger->Info(TAG, "==================== %s v%s ====================",
                                   Instance->description->project_name, Instance->description->version);

            // TODO: Only for debug, remove

            vTaskDelay(5000 / portTICK_PERIOD_MS);

            char taskInfoList[1024];
            char *taskInfo;

            vTaskList(taskInfoList);

            taskInfo = strtok(taskInfoList, "\n");

            Instance->logger->Debug(TAG, "=====================================================");
            Instance->logger->Debug(TAG, "Task             State   Prio    Free    Num     Core");
            Instance->logger->Debug(TAG, "-----------------------------------------------------");

            while (taskInfo != NULL)
            {
                Instance->logger->Debug(TAG, "%s", taskInfo);
                taskInfo = strtok(NULL, "\n");
            }

            Instance->logger->Debug(TAG, "=====================================================");

            // ----------------------------

            return Instance;
        }
    };
}

extern "C" void app_main(void)
{
    // Dependencies must be initialized after App main entrypoint
    main::App::New();
}
