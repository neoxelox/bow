#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include "logger.hpp"
#include "gpio.hpp"
#include "status.hpp"

namespace device
{
    static const char *TAG = "device";

    static const int MAX_SYNC_PULSES = 4;      // Max PROTOCOLS(Sync) * 2 (extra end sync)
    static const int MAX_PREAMBLE_PULSES = 24; // Max PROTOCOLS(Preamble)
    static const int MAX_DATA_PULSES = 130;    // Max PROTOCOLS(Data): 65 (x2) bits
    static const int MIN_DATA_PULSES = 64;     // Min PROTOCOLS(Data): 32 (x2) bits
    static const int MAX_DATA_ZERO_PULSES = 2; // Max PROTOCOLS(Data.Zero)
    static const int MAX_DATA_ONE_PULSES = 2;  // Max PROTOCOLS(Data.One)

    // NOTE: ESP32 cannot use floats in ISRs.

    static const int PULSE_WIDTH_TOLERANCE = 25;                                                      // Can be up to 25% different
    static const uint32_t MIN_PULSE_WIDTH = 100 * (1 - (float)(PULSE_WIDTH_TOLERANCE) / 100.0);       // Min PROTOCOLS(Width)
    static const uint32_t MIN_SYNC_PULSE_WIDTH = 2280 * (1 - (float)(PULSE_WIDTH_TOLERANCE) / 100.0); // Min PROTOCOLS(Width * Max(Sync))

    static const int QUEUE_SIZE = 25;
    static const int BUFFER_SIZE = MAX_SYNC_PULSES + MAX_PREAMBLE_PULSES + MAX_DATA_PULSES;

    class Protocol
    {
    public:
        struct Data
        {
            uint8_t Zero[MAX_DATA_ZERO_PULSES]; // Divisions
            uint8_t One[MAX_DATA_ONE_PULSES];   // Divisions
        };

        uint32_t Width;                        // Microseconds
        uint8_t Sync[MAX_SYNC_PULSES];         // Divisions
        uint8_t Preamble[MAX_PREAMBLE_PULSES]; // Divisions
        Data Data;
    };

    // Do NOT change the order of the protocols as their ID is their (index + 1) on this arrray
    // Follow the C zeroed-initialization style: {Width, {Sync}, {Preamble}, {{Zero}, {One}}}
    static const Protocol PROTOCOLS[] = {
        {400, {1, 42}, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 10}, {{1, 2}, {2, 1}}}, // Master Otello Blinds
        {300, {1, 34}, {}, {{1, 3}, {3, 1}}},                                                                        // SatChef Socket
        {450, {1, 29}, {}, {{1, 3}, {3, 1}}},                                                                        // Splenssy Alarm
        {360, {1, 25}, {13, 4}, {{1, 2}, {2, 1}}},                                                                   // Sublimex Blinds
        // https://github.com/sui77/rc-switch/blob/436a74b03f3dc17a29ee327af29d5a05d77f94b9/RCSwitch.cpp#L82
        {350, {1, 31}, {}, {{1, 3}, {3, 1}}},    // Common ten pole DIP switch
        {650, {1, 10}, {}, {{1, 2}, {2, 1}}},    // Common two rotary/sliding switch
        {100, {30, 71}, {}, {{4, 11}, {9, 6}}},  // Intertechno Socket
        {380, {1, 6}, {}, {{1, 3}, {3, 1}}},     // SilverCrest Socket v1
        {500, {6, 14}, {}, {{1, 2}, {2, 1}}},    // SilverCrest Socket v2
        {450, {1, 23}, {}, {{1, 2}, {2, 1}}},    // HT6P20B
        {150, {2, 62}, {}, {{1, 6}, {6, 1}}},    // HS2303-PT
        {200, {3, 130}, {}, {{7, 16}, {3, 16}}}, // Conrad RS-200 Rx
        {200, {7, 130}, {}, {{16, 7}, {16, 3}}}, // Conrad RS-200 Tx
        {365, {1, 18}, {}, {{3, 1}, {1, 3}}},    // 1ByOne Doorbell
        {270, {1, 36}, {}, {{1, 2}, {2, 1}}},    // HT12E
        {320, {1, 36}, {}, {{1, 2}, {2, 1}}},    // SM5212
    };

    static const int NUM_PROTOCOLS = sizeof(PROTOCOLS) / sizeof(Protocol);

    class Packet
    {
    public:
        uint8_t ProtocolID;
        char Data[MAX_DATA_PULSES + 1]; // '\0' terminated c-string
    };

    class Receiver
    {
    private:
        logger::Logger *logger;
        status::Controller *status;
        gpio::Digital *pin;
        TaskHandle_t taskHandle;
        QueueHandle_t queue;
        uint32_t isrBuffer[BUFFER_SIZE] = {};
        int isrIndex = 0;
        int64_t isrLastLastTime = 0;
        int64_t isrLastTime = 0;
        bool isrIsReceivingData = false;
        char isrData[MAX_DATA_PULSES + 1] = {}; // '\0' terminated c-string

    private:
        static void IRAM_ATTR isrFunc(void *args);
        static void taskFunc(void *args);
        bool decode(const Protocol *protocol);

    public:
        inline static Receiver *Instance;
        static Receiver *New(logger::Logger *logger, status::Controller *status);

    public:
        void Wait(Packet *packet);
    };

    class Transmitter
    {
    private:
        logger::Logger *logger;
        status::Controller *status;
        gpio::Digital *pin;
        TaskHandle_t taskHandle;
        QueueHandle_t queue;
        portMUX_TYPE lock;

    private:
        static void taskFunc(void *args);
        void sendPulses(uint32_t width, uint8_t const pulses[], size_t size);
        void encode(const Packet *packet, int replays);

    public:
        inline static Transmitter *Instance;
        static Transmitter *New(logger::Logger *logger, status::Controller *status);

    public:
        esp_err_t Send(Packet *packet);
    };
}