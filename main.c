#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define PARKING_BAY_COUNT 5
#define MONITOR_INTERVAL_MS 2000

typedef struct {
    int bay_id;
    int vehicle_id;
} ParkingBay;

/* A vehicle ID of 0 means the bay is available. */
static ParkingBay parking_bays[PARKING_BAY_COUNT] = {
    { 1, 0 },
    { 2, 0 },
    { 3, 0 },
    { 4, 0 },
    { 5, 0 }
};

static SemaphoreHandle_t parking_mutex;

static void parking_monitor_task(void *argument)
{
    ParkingBay snapshot[PARKING_BAY_COUNT];

    (void)argument;

    for (;;) {
        int available_bays = 0;
        unsigned long timestamp_ms;

        /* Copy the shared data while holding the mutex. */
        xSemaphoreTake(parking_mutex, portMAX_DELAY);

        for (int i = 0; i < PARKING_BAY_COUNT; i++) {
            snapshot[i] = parking_bays[i];

            if (snapshot[i].vehicle_id == 0) {
                available_bays++;
            }
        }

        timestamp_ms = (unsigned long)(
            xTaskGetTickCount() * portTICK_PERIOD_MS
        );

        xSemaphoreGive(parking_mutex);

        /*
         * Console output is kept together for this Windows
         * simulator demonstration.
         */
        taskENTER_CRITICAL();

        printf(
            "\n[%lu ms] MONITOR | Available: %d | Occupied: %d\n",
            timestamp_ms,
            available_bays,
            PARKING_BAY_COUNT - available_bays
        );

        for (int i = 0; i < PARKING_BAY_COUNT; i++) {
            if (snapshot[i].vehicle_id == 0) {
                printf(
                    "[%lu ms] Bay=%d | Vehicle=N/A | Status=AVAILABLE\n",
                    timestamp_ms,
                    snapshot[i].bay_id
                );
            } else {
                printf(
                    "[%lu ms] Bay=%d | Vehicle=%d | Status=OCCUPIED\n",
                    timestamp_ms,
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

    if (parking_mutex == NULL) {
        fputs("Could not create the parking mutex.\n", stderr);
        return 1;
    }

    if (xTaskCreate(
            parking_monitor_task,
            "ParkingMonitor",
            configMINIMAL_STACK_SIZE * 2,
            NULL,
            tskIDLE_PRIORITY + 1,
            NULL
        ) != pdPASS) {
        fputs("Could not create the parking monitor task.\n", stderr);
        return 1;
    }

    puts("Autonomous Campus Parking Management System");
    puts("Starting parking availability monitoring...");

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}