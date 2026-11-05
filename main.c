#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define PARKING_BAY_COUNT 5
#define VEHICLE_QUEUE_LENGTH 10
#define MONITOR_INTERVAL_MS 2000
#define ALLOCATION_INTERVAL_MS 100

typedef struct {
    int bay_id;
    int vehicle_id;
} ParkingBay;

typedef struct {
    int vehicle_id;
    BaseType_t is_vip;
} Vehicle;

/* A vehicle ID of 0 means the bay is available. */
static ParkingBay parking_bays[PARKING_BAY_COUNT] = {
    { 1, 0 },
    { 2, 0 },
    { 3, 0 },
    { 4, 0 },
    { 5, 0 }
};

static SemaphoreHandle_t parking_mutex;
static QueueHandle_t vip_queue;
static QueueHandle_t normal_queue;

static BaseType_t is_registered_vip(int vehicle_id)
{
    static const int vip_ids[] = { 102, 105 };
    const int vip_count = sizeof(vip_ids) / sizeof(vip_ids[0]);

    for (int i = 0; i < vip_count; i++) {
        if (vehicle_id == vip_ids[i]) {
            return pdTRUE;
        }
    }

    return pdFALSE;
}

static void print_vehicle_event(
    const char *action,
    const Vehicle *vehicle,
    int bay_id
)
{
    unsigned long timestamp_ms = (unsigned long)(
        xTaskGetTickCount() * portTICK_PERIOD_MS
    );

    taskENTER_CRITICAL();

    if (bay_id == 0) {
        printf(
            "[%lu ms] %s | Vehicle=%d | Type=%s | Bay=N/A\n",
            timestamp_ms,
            action,
            vehicle->vehicle_id,
            vehicle->is_vip ? "VIP" : "NORMAL"
        );
    } else {
        printf(
            "[%lu ms] %s | Vehicle=%d | Type=%s | Bay=%d\n",
            timestamp_ms,
            action,
            vehicle->vehicle_id,
            vehicle->is_vip ? "VIP" : "NORMAL",
            bay_id
        );
    }

    taskEXIT_CRITICAL();
}

static void vehicle_arrival_task(void *argument)
{
    static const int incoming_ids[] = {
        101, 102, 103, 104, 105, 106
    };

    const int vehicle_count =
        sizeof(incoming_ids) / sizeof(incoming_ids[0]);

    (void)argument;

    for (int i = 0; i < vehicle_count; i++) {
        Vehicle vehicle;
        QueueHandle_t selected_queue;

        vehicle.vehicle_id = incoming_ids[i];
        vehicle.is_vip = is_registered_vip(vehicle.vehicle_id);

        selected_queue = vehicle.is_vip
            ? vip_queue
            : normal_queue;

        /*
         * Arrivals and allocations use the same mutex.
         * This protects the VIP selection and allocation decision.
         */
        xSemaphoreTake(parking_mutex, portMAX_DELAY);

        if (xQueueSend(selected_queue, &vehicle, 0) == pdPASS) {
            print_vehicle_event("ENTRY", &vehicle, 0);
        } else {
            print_vehicle_event("QUEUE_FULL", &vehicle, 0);
        }

        xSemaphoreGive(parking_mutex);

        /* The second group arrives three seconds later. */
        if (i == 2) {
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    /* All six simulated arrivals have been generated. */
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

            /* Waiting VIPs always receive the next available bay. */
            received = xQueueReceive(vip_queue, &vehicle, 0);

            if (received != pdPASS) {
                received = xQueueReceive(normal_queue, &vehicle, 0);
            }

            if (received != pdPASS) {
                break;
            }

            parking_bays[i].vehicle_id = vehicle.vehicle_id;

            print_vehicle_event(
                "ASSIGNMENT",
                &vehicle,
                parking_bays[i].bay_id
            );
        }

        xSemaphoreGive(parking_mutex);

        vTaskDelay(pdMS_TO_TICKS(ALLOCATION_INTERVAL_MS));
    }
}

static void parking_monitor_task(void *argument)
{
    ParkingBay snapshot[PARKING_BAY_COUNT];

    (void)argument;

    for (;;) {
        int available_bays = 0;
        UBaseType_t waiting_vips;
        UBaseType_t waiting_normal;
        unsigned long timestamp_ms;

        xSemaphoreTake(parking_mutex, portMAX_DELAY);

        for (int i = 0; i < PARKING_BAY_COUNT; i++) {
            snapshot[i] = parking_bays[i];

            if (snapshot[i].vehicle_id == 0) {
                available_bays++;
            }
        }

        waiting_vips = uxQueueMessagesWaiting(vip_queue);
        waiting_normal = uxQueueMessagesWaiting(normal_queue);

        timestamp_ms = (unsigned long)(
            xTaskGetTickCount() * portTICK_PERIOD_MS
        );

        xSemaphoreGive(parking_mutex);

        taskENTER_CRITICAL();

        printf(
            "\n[%lu ms] MONITOR | Available=%d | Occupied=%d\n",
            timestamp_ms,
            available_bays,
            PARKING_BAY_COUNT - available_bays
        );

        printf(
            "Waiting: VIP=%lu | Normal=%lu\n",
            (unsigned long)waiting_vips,
            (unsigned long)waiting_normal
        );

        for (int i = 0; i < PARKING_BAY_COUNT; i++) {
            if (snapshot[i].vehicle_id == 0) {
                printf(
                    "Bay=%d | Vehicle=N/A | Status=AVAILABLE\n",
                    snapshot[i].bay_id
                );
            } else {
                printf(
                    "Bay=%d | Vehicle=%d | Status=OCCUPIED\n",
                    snapshot[i].bay_id,
                    snapshot[i].vehicle_id
                );
            }
        }

        taskEXIT_CRITICAL();

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
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

    if (parking_mutex == NULL ||
        vip_queue == NULL ||
        normal_queue == NULL) {
        fputs("Could not create parking resources.\n", stderr);
        return 1;
    }

    /*
     * Arrivals have the highest application priority so each
     * simulated group is queued before allocation begins.
     */
    if (xTaskCreate(
            vehicle_arrival_task,
            "VehicleArrival",
            configMINIMAL_STACK_SIZE * 2,
            NULL,
            tskIDLE_PRIORITY + 3,
            NULL
        ) != pdPASS ||
        xTaskCreate(
            bay_allocation_task,
            "BayAllocation",
            configMINIMAL_STACK_SIZE * 2,
            NULL,
            tskIDLE_PRIORITY + 2,
            NULL
        ) != pdPASS ||
        xTaskCreate(
            parking_monitor_task,
            "ParkingMonitor",
            configMINIMAL_STACK_SIZE * 2,
            NULL,
            tskIDLE_PRIORITY + 1,
            NULL
        ) != pdPASS) {
        fputs("Could not create parking tasks.\n", stderr);
        return 1;
    }

    puts("Autonomous Campus Parking Management System");
    puts("Registered VIP vehicles: 102 and 105");

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}