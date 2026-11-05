#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define PARKING_BAY_COUNT 5
#define VEHICLE_QUEUE_LENGTH 10
#define LOG_QUEUE_LENGTH 64

#define MONITOR_INTERVAL_MS 2000
#define ALLOCATION_INTERVAL_MS 100
#define RELEASE_INTERVAL_MS 250
#define PARKING_DURATION_MS 8000

typedef struct {
    int vehicle_id;
    BaseType_t is_vip;
} Vehicle;

typedef struct {
    int vehicle_id;
    TickType_t occupied_since;
} ParkingBay;

typedef enum {
    EVENT_ARRIVAL,
    EVENT_ASSIGNMENT,
    EVENT_EXIT,
    EVENT_RELEASE,
    EVENT_QUEUE_FULL,
    EVENT_MONITOR
} EventAction;

typedef struct {
    TickType_t timestamp;
    EventAction action;
    int vehicle_id;
    int bay_id;
    BaseType_t is_vip;
    int free_slots;
} LogEvent;

/* Array positions represent slots P0 to P4. */
static ParkingBay parking_bays[PARKING_BAY_COUNT];

static SemaphoreHandle_t parking_mutex;
static QueueHandle_t vip_queue;
static QueueHandle_t normal_queue;
static QueueHandle_t log_queue;

static BaseType_t is_registered_vip(int vehicle_id)
{
    return (
        vehicle_id == 900 || vehicle_id == 901
    ) ? pdTRUE : pdFALSE;
}

static unsigned long long ticks_to_ms(TickType_t ticks)
{
    return (
        (unsigned long long)ticks * 1000ULL
    ) / configTICK_RATE_HZ;
}

static void log_event(
    EventAction action,
    const Vehicle *vehicle,
    int bay_id,
    int free_slots
)
{
    LogEvent event;

    event.timestamp = xTaskGetTickCount();
    event.action = action;
    event.vehicle_id = vehicle != NULL ? vehicle->vehicle_id : 0;
    event.is_vip = vehicle != NULL ? vehicle->is_vip : pdFALSE;
    event.bay_id = bay_id;
    event.free_slots = free_slots;

    BaseType_t result = xQueueSend(
        log_queue,
        &event,
        portMAX_DELAY
    );

    configASSERT(result == pdPASS);
    (void)result;
}

static void logging_task(void *argument)
{
    LogEvent event;

    (void)argument;

    for (;;) {
        if (xQueueReceive(
                log_queue,
                &event,
                portMAX_DELAY
            ) != pdPASS) {
            continue;
        }

        unsigned long long timestamp_ms =
            ticks_to_ms(event.timestamp);

        taskENTER_CRITICAL();

        switch (event.action) {
            case EVENT_ARRIVAL:
                printf(
                    "[ARRIVAL] %sVehicle V%d entered the parking area"
                    " | Slot N/A | Time: %llu ms\n",
                    event.is_vip ? "VIP " : "",
                    event.vehicle_id,
                    timestamp_ms
                );
                break;

            case EVENT_ASSIGNMENT:
                if (event.is_vip) {
                    printf(
                        "[VIP] VIP Vehicle V%d assigned to Slot P%d"
                        " | Time: %llu ms\n",
                        event.vehicle_id,
                        event.bay_id,
                        timestamp_ms
                    );
                } else {
                    printf(
                        "[ENTRY] Vehicle V%d assigned to Slot P%d"
                        " | Time: %llu ms\n",
                        event.vehicle_id,
                        event.bay_id,
                        timestamp_ms
                    );
                }
                break;

            case EVENT_EXIT:
                printf(
                    "[EXIT] Vehicle V%d left Slot P%d"
                    " | Time: %llu ms\n",
                    event.vehicle_id,
                    event.bay_id,
                    timestamp_ms
                );
                break;

            case EVENT_RELEASE:
                printf(
                    "[RELEASE] Slot P%d released from Vehicle V%d"
                    " | Time: %llu ms\n",
                    event.bay_id,
                    event.vehicle_id,
                    timestamp_ms
                );
                break;

            case EVENT_QUEUE_FULL:
                printf(
                    "[QUEUE FULL] Vehicle V%d could not join the queue"
                    " | Slot N/A | Time: %llu ms\n",
                    event.vehicle_id,
                    timestamp_ms
                );
                break;

            case EVENT_MONITOR:
                printf(
                    "\n[PARKING MONITOR] Free Slots: %d"
                    " | Time: %llu ms\n\n",
                    event.free_slots,
                    timestamp_ms
                );
                break;
        }

        taskEXIT_CRITICAL();
    }
}

static void vehicle_arrival_task(void *argument)
{
    static const int incoming_ids[] = {
        101, 102, 103, 900, 104, 901
    };

    const int vehicle_count =
        sizeof(incoming_ids) / sizeof(incoming_ids[0]);

    (void)argument;

    /* Allow the monitor to display the initial five free slots. */
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (int i = 0; i < vehicle_count; i++) {
        Vehicle vehicle;
        QueueHandle_t selected_queue;

        vehicle.vehicle_id = incoming_ids[i];
        vehicle.is_vip = is_registered_vip(vehicle.vehicle_id);

        selected_queue = vehicle.is_vip
            ? vip_queue
            : normal_queue;

        xSemaphoreTake(parking_mutex, portMAX_DELAY);

        log_event(EVENT_ARRIVAL, &vehicle, -1, 0);

        if (xQueueSend(selected_queue, &vehicle, 0) != pdPASS) {
            log_event(EVENT_QUEUE_FULL, &vehicle, -1, 0);
        }

        xSemaphoreGive(parking_mutex);

        /*
         * Group 1: V101 and V102.
         * Group 2: V103, VIP V900 and V104.
         * Group 3: VIP V901.
         */
        if (i == 1 || i == 4) {
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    for (;;) {
        vTaskSuspend(NULL);
    }
}

static void bay_allocation_task(void *argument)
{
    (void)argument;

    for (;;) {
        xSemaphoreTake(parking_mutex, portMAX_DELAY);

        for (int i = 0; i < PARKING_BAY_COUNT; i++) {
            Vehicle vehicle;
            BaseType_t received;

            if (parking_bays[i].vehicle_id != 0) {
                continue;
            }

            /* Always check the VIP queue first. */
            received = xQueueReceive(vip_queue, &vehicle, 0);

            if (received != pdPASS) {
                received = xQueueReceive(normal_queue, &vehicle, 0);
            }

            if (received != pdPASS) {
                break;
            }

            parking_bays[i].vehicle_id = vehicle.vehicle_id;
            parking_bays[i].occupied_since = xTaskGetTickCount();

            log_event(EVENT_ASSIGNMENT, &vehicle, i, 0);
        }

        xSemaphoreGive(parking_mutex);

        vTaskDelay(pdMS_TO_TICKS(ALLOCATION_INTERVAL_MS));
    }
}

static void bay_release_task(void *argument)
{
    (void)argument;

    for (;;) {
        xSemaphoreTake(parking_mutex, portMAX_DELAY);

        TickType_t current_time = xTaskGetTickCount();

        for (int i = 0; i < PARKING_BAY_COUNT; i++) {
            if (parking_bays[i].vehicle_id == 0) {
                continue;
            }

            TickType_t elapsed_time = (TickType_t)(
                current_time - parking_bays[i].occupied_since
            );

            /* Simulate the vehicle exiting after eight seconds. */
            if (elapsed_time >= pdMS_TO_TICKS(PARKING_DURATION_MS)) {
                Vehicle departing_vehicle;

                departing_vehicle.vehicle_id =
                    parking_bays[i].vehicle_id;

                departing_vehicle.is_vip = is_registered_vip(
                    departing_vehicle.vehicle_id
                );

                log_event(EVENT_EXIT, &departing_vehicle, i, 0);

                parking_bays[i].vehicle_id = 0;
                parking_bays[i].occupied_since = 0;

                log_event(EVENT_RELEASE, &departing_vehicle, i, 0);
            }
        }

        xSemaphoreGive(parking_mutex);

        vTaskDelay(pdMS_TO_TICKS(RELEASE_INTERVAL_MS));
    }
}

static void parking_monitor_task(void *argument)
{
    (void)argument;

    for (;;) {
        int free_slots = 0;

        xSemaphoreTake(parking_mutex, portMAX_DELAY);

        for (int i = 0; i < PARKING_BAY_COUNT; i++) {
            if (parking_bays[i].vehicle_id == 0) {
                free_slots++;
            }
        }

        log_event(EVENT_MONITOR, NULL, -1, free_slots);

        xSemaphoreGive(parking_mutex);

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

static BaseType_t create_parking_task(
    TaskFunction_t function,
    const char *name,
    UBaseType_t priority
)
{
    return xTaskCreate(
        function,
        name,
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        priority,
        NULL
    );
}

int main(void)
{
    parking_mutex = xSemaphoreCreateMutex();

    vip_queue = xQueueCreate(
        VEHICLE_QUEUE_LENGTH,
        sizeof(Vehicle)
    );

    normal_queue = xQueueCreate(
        VEHICLE_QUEUE_LENGTH,
        sizeof(Vehicle)
    );

    log_queue = xQueueCreate(
        LOG_QUEUE_LENGTH,
        sizeof(LogEvent)
    );

    if (parking_mutex == NULL ||
        vip_queue == NULL ||
        normal_queue == NULL ||
        log_queue == NULL) {
        fputs("Could not create parking resources.\n", stderr);
        return 1;
    }

    if (create_parking_task(
            logging_task,
            "Logging",
            tskIDLE_PRIORITY + 4
        ) != pdPASS ||
        create_parking_task(
            vehicle_arrival_task,
            "VehicleArrival",
            tskIDLE_PRIORITY + 3
        ) != pdPASS ||
        create_parking_task(
            bay_allocation_task,
            "BayAllocation",
            tskIDLE_PRIORITY + 2
        ) != pdPASS ||
        create_parking_task(
            bay_release_task,
            "BayRelease",
            tskIDLE_PRIORITY + 2
        ) != pdPASS ||
        create_parking_task(
            parking_monitor_task,
            "ParkingMonitor",
            tskIDLE_PRIORITY + 1
        ) != pdPASS) {
        fputs("Could not create parking tasks.\n", stderr);
        return 1;
    }

    puts("Autonomous Campus Parking Management System");
    puts("Timestamps show milliseconds since scheduler startup.");

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}