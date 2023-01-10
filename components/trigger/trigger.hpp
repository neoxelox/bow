#pragma once

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.hpp"
#include "chron.hpp"
#include "device.hpp"
#include "database.hpp"

namespace trigger
{
    static const char *TAG = "trigger";

    static const char *DB_NAMESPACE = "trigger";
    static const TickType_t SCHEDULER_PERIOD = (1 * 60 * 1000) / portTICK_PERIOD_MS; // 1 minute

    class Trigger
    {
    public:
        const char *Name;
        const char *Actuator;
        const char *Schedule;
        const char *Emoji;
        const char *Creator;
        time_t CreatedAt;

    public:
        Trigger();
        Trigger(const char *name, const char *actuator, const char *schedule,
                const char *emoji, const char *creator, time_t createdAt);
        Trigger(cJSON *src);
        ~Trigger();
        Trigger &operator=(const Trigger &other);
        cJSON *JSON();
        bool Equals(const char *name) const;
        bool Equals(Trigger *other) const;
        bool Equals(const Trigger *other) const;
    };

    class Controller
    {
    private:
        logger::Logger *logger;
        chron::Controller *chron;
        device::Transmitter *transmitter;
        device::Controller *device;
        database::Handle *db;
        TaskHandle_t taskHandle;

    private:
        const char *fixSchedule(const char *schedule);
        static void taskFunc(void *args);

    public:
        inline static Controller *Instance;
        static Controller *New(logger::Logger *logger, chron::Controller *chron, device::Transmitter *transmitter,
                               device::Controller *device, database::Database *database);

    public:
        uint32_t Count();
        Trigger *Get(const char *name);
        Trigger *List(uint32_t *size);
        void Set(Trigger *trigger);
        void DeleteByName(const char *name);
        void DeleteByActuator(const char *actuator);
        void Drop();
        bool IsScheduleValid(const char *schedule);
    };
}