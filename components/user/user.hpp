#pragma once

#include "cJSON.h"
#include "logger.hpp"
#include "database.hpp"

namespace user
{
    static const char *TAG = "user";

    static const char *DB_NAMESPACE = "user";
    static const uint8_t TOKEN_SIZE = 16;
    static const char *TOKEN_CHARSET = "0123456789abcdefghijklmnopqrstuvwxyz";

    class User
    {
    public:
        const char *Name;
        const char *Password;
        const char *Token;
        const char *Role;
        const char *Emoji;
        time_t CreatedAt;

    public:
        User();
        User(const char *name, const char *password, const char *token,
             const char *role, const char *emoji, time_t createdAt);
        User(cJSON *src);
        ~User();
        User &operator=(const User &other);
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
        User *Get(const char *name);
        User *List(uint32_t *size);
        void Set(User *user);
        void Delete(const char *name);
        void Drop();
        void GenerateToken(char *token);
    };
}