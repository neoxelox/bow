#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "logger.hpp"
#include "gpio.hpp"
#include "status.hpp"
#include "device.hpp"

namespace device
{
    Receiver *Receiver::New(logger::Logger *logger, status::Controller *status)
    {
        if (Instance != NULL)
            return Instance;

        Instance = new Receiver();

        // Inject dependencies
        Instance->logger = logger;
        Instance->status = status;

        // Initialize receiver queue
        Instance->queue = xQueueCreate(QUEUE_SIZE, sizeof(Packet *));
        if (!Instance->queue)
            ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

        // Initialize receiver
        Instance->pin = gpio::Digital::New(GPIO_NUM_17, GPIO_MODE_INPUT);
        Instance->pin->AttachPullResistor(GPIO_PULLDOWN_ONLY);
        Instance->pin->AttachInterrupt(GPIO_INTR_ANYEDGE, Instance->isrFunc, NULL);

        // Create receiver task
        xTaskCreatePinnedToCore(Instance->taskFunc, "Receiver", 4 * 1024, NULL, 10, &Instance->taskHandle, 0);

        return Instance;
    }

    void Receiver::Wait(Packet *packet)
    {
        xQueueReceive(this->queue, &packet, portMAX_DELAY);
    }

    int diff(int a, int b)
    {
        return ((b - a) * 100) / (a + 1); // Avoid dividing by zero
    }

    void IRAM_ATTR Receiver::isrFunc(void *args)
    {
        int64_t isrCurrentTime = esp_timer_get_time();

        uint32_t isrPulseWidth = isrCurrentTime - Instance->isrLastTime;

        // Ignore short pulses which can be noise and may split actual pulses
        if (isrPulseWidth < MIN_PULSE_WIDTH)
        {
            Instance->isrLastTime = Instance->isrLastLastTime;

            if (Instance->isrIndex > 0)
                Instance->isrIndex--;

            return;
        }

        // Advance times
        Instance->isrLastLastTime = Instance->isrLastTime;
        Instance->isrLastTime = isrCurrentTime;

        // Long pulses can be the start or end of a data packet
        if (isrPulseWidth >= MIN_SYNC_PULSE_WIDTH)
        {
            // We weren't receiving the packet so it's the start
            if (!Instance->isrIsReceivingData)
            {
                Instance->isrIsReceivingData = true;
                Instance->isrIndex = 0;
            }
            // We were receiving the packet so it's the end
            else
            {
                int isrPulseDifference = diff(Instance->isrBuffer[0], isrPulseWidth);

                // If both pulses differ to much its not the actual end
                if (abs(isrPulseDifference) > PULSE_WIDTH_TOLERANCE)
                {
                    // If current pulse is greater maybe its a new packet
                    if (isrPulseDifference > 0)
                        Instance->isrIndex = 0;
                    // Otherwise ignore as it could be a second sync or the space between the preamble and data
                }
                // Pulses are similar, it could indeed be a packet
                else
                {
                    // Ignore small packets which can be noise
                    if ((Instance->isrIndex - 2) >= MIN_DATA_PULSES)
                    {
                        uint8_t protocolID;

                        // Try to decode the packet with all known protocols
                        for (protocolID = 0; protocolID < NUM_PROTOCOLS; protocolID++)
                            if (Instance->decode(&PROTOCOLS[protocolID]))
                                break;

                        // The packet has been successfully decoded
                        if (protocolID < NUM_PROTOCOLS)
                        {
                            // Publish packet to the queue
                            Packet *packet = new Packet{++protocolID};
                            memcpy(packet->Data, Instance->isrData, MAX_DATA_PULSES + 1);
                            xQueueSendFromISR(Instance->queue, &packet, NULL);
                        }
                    }

                    Instance->isrIsReceivingData = false;
                }
            }
        }

        // Save pulse into the buffer if we are receiving a packet
        if (Instance->isrIsReceivingData)
        {
            Instance->isrBuffer[Instance->isrIndex] = isrPulseWidth;

            // Avoid buffer overflow
            if (Instance->isrIndex >= (BUFFER_SIZE - 1))
                Instance->isrIndex = 0;
            else
                Instance->isrIndex++;
        }
    }

    bool Receiver::decode(const Protocol *protocol)
    {
        int ignoreSyncStartPulses = 0;
        int ignoreSyncEndPulses = 0;
        int i;

        // Localize where we are in the sync phase at the start and at the end
        for (i = 0; i < MAX_SYNC_PULSES; i++)
        {
            if (protocol->Sync[i] == 0)
                break;

            if (ignoreSyncStartPulses == 0 &&
                abs(diff(protocol->Sync[i] * protocol->Width, this->isrBuffer[0])) <= PULSE_WIDTH_TOLERANCE)
                ignoreSyncStartPulses = i;
        }
        ignoreSyncStartPulses++;
        ignoreSyncEndPulses = (i - ignoreSyncStartPulses) + 1;

        int ignorePreamblePulses;

        // Get number of preamble pulses
        for (ignorePreamblePulses = 0; ignorePreamblePulses < MAX_PREAMBLE_PULSES; ignorePreamblePulses++)
            if (protocol->Preamble[ignorePreamblePulses] == 0)
                break;

        // Narrow down the actual data
        int dataStart = ignoreSyncEndPulses + ignorePreamblePulses;
        int dataEnd = this->isrIndex - ignoreSyncStartPulses;

        // Ignore small packets which can be noise or giant packets that don't fit
        if ((dataEnd - dataStart + 1) < MIN_DATA_PULSES ||
            (dataEnd - dataStart + 1) > MAX_DATA_PULSES)
            return false;

        int bit = 0;
        int pulse = dataStart;
        while (pulse <= dataEnd)
        {
            // Try zero
            for (i = 0; i < MAX_DATA_ZERO_PULSES; i++)
            {
                if (protocol->Data.Zero[i] == 0)
                    break;

                // If it's not zero try one
                if (((pulse + i) > dataEnd) ||
                    abs(diff(protocol->Data.Zero[i] * protocol->Width, this->isrBuffer[pulse + i])) > PULSE_WIDTH_TOLERANCE)
                    goto one;
            }

            this->isrData[bit++] = '0';
            pulse += i;
            continue;
        // Try one
        one:
            for (i = 0; i < MAX_DATA_ONE_PULSES; i++)
            {
                if (protocol->Data.One[i] == 0)
                    break;

                // If it's not one either it's not this protocol
                if (((pulse + i) > dataEnd) ||
                    abs(diff(protocol->Data.One[i] * protocol->Width, this->isrBuffer[pulse + i])) > PULSE_WIDTH_TOLERANCE)
                    return false;
            }

            this->isrData[bit++] = '1';
            pulse += i;
            continue;
        }

        this->isrData[bit] = '\0';

        return true;
    }

    // TODO: makes sense having this task -> no, the packet is even deleted -> care!
    void Receiver::taskFunc(void *args)
    {
        Packet *packet;

        while (1)
        {
            xQueueReceive(Instance->queue, &packet, portMAX_DELAY);

            Instance->logger->Debug(TAG, "Rx | Data: %s | Protocol: %d", packet->Data, packet->ProtocolID);
            Instance->status->SetStatus(status::Statuses::Received);

            delete packet;
        }
    }
}