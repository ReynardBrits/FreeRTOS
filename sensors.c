#include <stdio.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define PRESSURE_INTERVAL_MS 5000
#define VIBRATION_INTERVAL_MS 7000
#define DISPLAY_INTERVAL_MS 1000
#define LOG_QUEUE_LENGTH 32

typedef struct {
    float pressure;
    float vibration;
} SensorReadings;

typedef enum {
    SENSOR_PRESSURE,
    SENSOR_VIBRATION
} SensorType;

typedef struct {
    TickType_t timestamp;
    SensorType updated_sensor;
    SensorReadings readings;
} SensorLogEvent;

/* Initial simulated readings. */
static SensorReadings shared_readings = {
    101.50f,
    0.58f
};

static SemaphoreHandle_t sensor_mutex;
static QueueHandle_t log_queue;

static uint32_t next_random(uint32_t *state)
{
    *state = (*state * 1664525U) + 1013904223U;
    return *state;
}

static unsigned long long ticks_to_ms(TickType_t ticks)
{
    return (
        (unsigned long long)ticks * 1000ULL
    ) / configTICK_RATE_HZ;
}

static SensorReadings get_sensor_snapshot(void)
{
    SensorReadings snapshot;

    xSemaphoreTake(sensor_mutex, portMAX_DELAY);

    snapshot = shared_readings;

    xSemaphoreGive(sensor_mutex);

    return snapshot;
}

static void update_sensor(SensorType sensor, float value)
{
    SensorLogEvent event;
    BaseType_t result;

    xSemaphoreTake(sensor_mutex, portMAX_DELAY);

    if (sensor == SENSOR_PRESSURE) {
        shared_readings.pressure = value;
    } else {
        shared_readings.vibration = value;
    }

    /* Capture the readings and timestamp when the update happens. */
    event.timestamp = xTaskGetTickCount();
    event.updated_sensor = sensor;
    event.readings = shared_readings;

    /*
     * Queue a copy of this update before another sensor changes
     * the shared readings. Wait for space rather than dropping it.
     *
     * The logging task does not acquire sensor_mutex, so it can
     * drain the queue while a producer waits for space.
     */
    result = xQueueSend(log_queue, &event, portMAX_DELAY);

    configASSERT(result == pdPASS);
    (void)result;

    xSemaphoreGive(sensor_mutex);
}

static void pressure_task(void *argument)
{
    uint32_t random_state = 12345U;
    float pressure = 101.50f;
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        update_sensor(SENSOR_PRESSURE, pressure);

        xTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(PRESSURE_INTERVAL_MS)
        );

        /* Generate the next pressure reading: 95.00–110.00 kPa. */
        pressure = 95.0f + (
            (float)(next_random(&random_state) % 1501U) / 100.0f
        );
    }
}

static void vibration_task(void *argument)
{
    uint32_t random_state = 67890U;
    float vibration = 0.58f;
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        update_sensor(SENSOR_VIBRATION, vibration);

        xTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(VIBRATION_INTERVAL_MS)
        );

        /* Generate the next vibration reading: 0.50–0.70 g. */
        vibration = 0.50f + (
            (float)(next_random(&random_state) % 21U) / 100.0f
        );
    }
}

static void display_task(void *argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        SensorReadings snapshot = get_sensor_snapshot();

        /* Keep each console message together in the simulator. */
        taskENTER_CRITICAL();

        printf(
            "[SENSOR] Pressure: %.2f kPa | Vibration: %.2f g\n",
            (double)snapshot.pressure,
            (double)snapshot.vibration
        );

        taskEXIT_CRITICAL();

        xTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(DISPLAY_INTERVAL_MS)
        );
    }
}

static void logging_task(void *argument)
{
    SensorLogEvent event;

    (void)argument;

    for (;;) {
        /*
         * Wait for an actual sensor update instead of logging
         * only at fixed ten-second intervals.
         */
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

        printf(
            "[LOG] %llums P: %.2f V: %.2f | Updated: %s\n",
            timestamp_ms,
            (double)event.readings.pressure,
            (double)event.readings.vibration,
            event.updated_sensor == SENSOR_PRESSURE
                ? "PRESSURE"
                : "VIBRATION"
        );

        taskEXIT_CRITICAL();
    }
}

static BaseType_t create_sensor_task(
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
    sensor_mutex = xSemaphoreCreateMutex();

    log_queue = xQueueCreate(
        LOG_QUEUE_LENGTH,
        sizeof(SensorLogEvent)
    );

    if (sensor_mutex == NULL || log_queue == NULL) {
        fputs("Could not create sensor resources.\n", stderr);
        return 1;
    }

    if (create_sensor_task(
            logging_task,
            "SensorLogger",
            tskIDLE_PRIORITY + 3
        ) != pdPASS ||
        create_sensor_task(
            pressure_task,
            "Pressure",
            tskIDLE_PRIORITY + 2
        ) != pdPASS ||
        create_sensor_task(
            vibration_task,
            "Vibration",
            tskIDLE_PRIORITY + 2
        ) != pdPASS ||
        create_sensor_task(
            display_task,
            "SensorDisplay",
            tskIDLE_PRIORITY + 1
        ) != pdPASS) {
        fputs("Could not create sensor tasks.\n", stderr);
        return 1;
    }

    puts("Automated Industrial Sensor Monitoring System");
    puts("Pressure updates every 5 seconds.");
    puts("Vibration updates every 7 seconds.");
    puts("Each sensor update is logged with its timestamp.\n");

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}