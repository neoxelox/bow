#pragma once

#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"

namespace trigger
{
    static const char *TAG = "trigger";

    static const char *DB_NAMESPACE = "trigger";

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
    };

    class Controller
    {
    private:
        logger::Logger *logger;
        database::Handle *db;

    public:
        inline static Controller *Instance;
        static Controller *New(logger::Logger *logger, database::Database *database);

    public:
        uint32_t Count();
        Trigger *Get(const char *name);
        Trigger *List(uint32_t *size);
        void Set(Trigger *trigger);
        void Delete(const char *name);
        void Drop();
    };
}