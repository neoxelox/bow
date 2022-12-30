#pragma once

#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"

namespace role
{
    static const char *TAG = "role";

    static const char *DB_NAMESPACE = "role";

    namespace System
    {
        static const char *ADMIN = "ADMIN";
        static const char *MEMBER = "MEMBER";
    }

    class Role
    {
    public:
        const char *Name;
        const char **Devices; // NULL-terminated array of c-strings
        const char *Emoji;
        const char *Creator;
        time_t CreatedAt;

    public:
        Role();
        Role(const char *name, const char **devices, const char *emoji,
             const char *creator, time_t createdAt);
        Role(cJSON *src);
        ~Role();
        Role &operator=(const Role &other);
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
        Role *Get(const char *name);
        Role *List(uint32_t *size);
        void Set(Role *role);
        void Delete(const char *name);
        void Drop();
    };
}