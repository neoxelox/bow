#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "logger.hpp"
#include "gpio.hpp"
#include "device.hpp"

namespace device
{
    Transmitter *Transmitter::New(logger::Logger *logger)
    {
        Transmitter *transmitter = new Transmitter();

        transmitter->logger = logger;

        transmitter->queue = xQueueCreate(QUEUE_SIZE, sizeof(Packet *));
        if (!transmitter->queue)
            ESP_ERROR_CHECK(ESP_ERR_NO_MEM);

        transmitter->pin = gpio::Digital::New(GPIO_NUM_38, GPIO_MODE_OUTPUT);
        transmitter->pin->AttachPullResistor(GPIO_PULLDOWN_ONLY);

        transmitter->lock = portMUX_INITIALIZER_UNLOCKED;

        xTaskCreatePinnedToCore(transmitter->taskFunc, "Transmitter", 4 * 1024, transmitter, 10, &transmitter->taskHandle, 1);

        return transmitter;
    }

    esp_err_t Transmitter::Send(const Packet *packet)
    {
        if (xQueueSend(this->queue, &packet, 0) == errQUEUE_FULL)
            return ESP_FAIL;

        return ESP_OK;
    }

    void Transmitter::taskFunc(void *args)
    {
        Transmitter *This = (Transmitter *)args;

        Packet *packet;

        while (1)
        {
            xQueueReceive(This->queue, &packet, portMAX_DELAY);
            This->logger->Debug(TAG, "Tx | Data: %s | Protocol: %d", packet->Data, packet->ProtocolID);

            This->encode(packet, 3);

            delete packet;
        }
    }

    void Transmitter::encode(const Packet *packet, int replays)
    {
        const Protocol *protocol = &PROTOCOLS[packet->ProtocolID - 1];

        taskENTER_CRITICAL(&(this->lock));

        this->pin->SetLevel(gpio::LOW);

        for (int k = 0; k < replays; k++)
        {
            // Sync phase
            this->sendPulses(protocol->Width, protocol->Sync, MAX_SYNC_PULSES);

            // Preamble phase
            this->sendPulses(protocol->Width, protocol->Preamble, MAX_PREAMBLE_PULSES);

            // Data phase
            for (int c = 0; packet->Data[c] != '\0'; c++)
            {
                if (packet->Data[c] == '1')
                    this->sendPulses(protocol->Width, protocol->Data.One, MAX_DATA_ONE_PULSES);
                else
                    this->sendPulses(protocol->Width, protocol->Data.Zero, MAX_DATA_ZERO_PULSES);
            }
        }

        this->pin->SetLevel(gpio::LOW);

        taskEXIT_CRITICAL(&(this->lock));
    }

    inline void Transmitter::sendPulses(uint32_t width, uint8_t const pulses[], size_t size)
    {
        uint32_t level = gpio::HIGH;

        for (int i = 0; i < size; i++)
        {
            if (pulses[i] == 0)
                break;

            this->pin->SetLevel(level);
            esp_rom_delay_us(pulses[i] * width);
            level = !level;
        }
    }
}