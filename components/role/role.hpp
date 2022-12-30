#pragma once

#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"

namespace role
{
    static const char *TAG = "role";

    static const char *DB_NAMESPACE = "role";

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

    namespace System
    {
        static const Role Admin = {"Admin", (const char **)calloc(1, sizeof(char *)), "0x1f6e1", "System", 0};
        static const Role Guest = {"Guest", (const char **)calloc(1, sizeof(char *)), "0x1f441", "System", 0};
    }

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