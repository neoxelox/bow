#pragma once

#include "cJSON.h"
#include "logger.hpp"
#include "role.hpp"
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
        bool Equals(const char *name) const;
        bool Equals(User *other) const;
        bool Equals(const User *other) const;
    };

    namespace System
    {
        static const User System = {"System", "", "", "Admin", "0x1f916", 0};
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
        User *Get(const char *name);
        User *List(uint32_t *size);
        void Set(User *user);
        void Delete(const char *name);
        void Drop();
        void GenerateToken(char *token);
        bool Belongs(const char *user, const char *role);
        bool Belongs(User *user, const char *role);
        bool Belongs(const char *user, role::Role *role);
        bool Belongs(const char *user, const role::Role *role);
        bool Belongs(User *user, role::Role *role);
        bool Belongs(User *user, const role::Role *role);
    };
}