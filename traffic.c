#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define GREEN_DURATION_MS 10000
#define YELLOW_DURATION_MS 2000
#define DISPLAY_INTERVAL_MS 1000
#define LOG_QUEUE_LENGTH 16

typedef enum {
    LIGHT_RED,
    LIGHT_YELLOW,
    LIGHT_GREEN
} LightState;

typedef struct {
    LightState north_south;
    LightState east_west;
} TrafficState;

typedef struct {
    TickType_t timestamp;
    TrafficState state;
} TrafficLogEvent;

static TrafficState traffic_state = {
    LIGHT_RED,
    LIGHT_RED
};

static QueueHandle_t log_queue;

static const char *light_name(LightState light)
{
    switch (light) {
        case LIGHT_GREEN:
            return "GREEN";

        case LIGHT_YELLOW:
            return "YELLOW";

        case LIGHT_RED:
            return "RED";

        default:
            return "UNKNOWN";
    }
}

static unsigned long long ticks_to_ms(TickType_t ticks)
{
    return (
        (unsigned long long)ticks * 1000ULL
    ) / configTICK_RATE_HZ;
}

static TrafficState get_traffic_snapshot(void)
{
    TrafficState snapshot;

    /* Read both directions together without a partial update. */
    taskENTER_CRITICAL();

    snapshot = traffic_state;

    taskEXIT_CRITICAL();

    return snapshot;
}

static TickType_t set_lights(
    LightState north_south,
    LightState east_west
)
{
    static BaseType_t first_transition = pdTRUE;
    static TickType_t start_tick;

    TrafficLogEvent event;
    TickType_t transition_tick;
    BaseType_t result;

    /* At least one direction must remain red. */
    configASSERT(
        north_south == LIGHT_RED || east_west == LIGHT_RED
    );

    taskENTER_CRITICAL();

    traffic_state.north_south = north_south;
    traffic_state.east_west = east_west;

    transition_tick = xTaskGetTickCount();

    if (first_transition == pdTRUE) {
        start_tick = transition_tick;
        first_transition = pdFALSE;
    }

    event.timestamp = (TickType_t)(
        transition_tick - start_tick
    );

    event.state = traffic_state;

    taskEXIT_CRITICAL();

    /* Send a copy of the transition to the logging task. */
    result = xQueueSend(log_queue, &event, portMAX_DELAY);

    configASSERT(result == pdPASS);
    (void)result;

    return transition_tick;
}

static void hold_phase(
    LightState north_south,
    LightState east_west,
    unsigned long duration_ms
)
{
    TickType_t phase_start = set_lights(
        north_south,
        east_west
    );

    xTaskDelayUntil(
        &phase_start,
        pdMS_TO_TICKS(duration_ms)
    );
}

static void traffic_controller_task(void *argument)
{
    (void)argument;

    for (;;) {
        hold_phase(
            LIGHT_GREEN,
            LIGHT_RED,
            GREEN_DURATION_MS
        );

        hold_phase(
            LIGHT_YELLOW,
            LIGHT_RED,
            YELLOW_DURATION_MS
        );

        hold_phase(
            LIGHT_RED,
            LIGHT_GREEN,
            GREEN_DURATION_MS
        );

        hold_phase(
            LIGHT_RED,
            LIGHT_YELLOW,
            YELLOW_DURATION_MS
        );
    }
}

static void logging_task(void *argument)
{
    TrafficLogEvent event;

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

        printf(
            "[LOG] %llums NS %s EW %s\n",
            timestamp_ms,
            light_name(event.state.north_south),
            light_name(event.state.east_west)
        );

        taskEXIT_CRITICAL();
    }
}

static void display_task(void *argument)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        TrafficState snapshot = get_traffic_snapshot();

        taskENTER_CRITICAL();

        printf(
            "[TRAFFIC] NS %s EW %s\n",
            light_name(snapshot.north_south),
            light_name(snapshot.east_west)
        );

        taskEXIT_CRITICAL();

        xTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(DISPLAY_INTERVAL_MS)
        );
    }
}

static BaseType_t create_traffic_task(
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
    log_queue = xQueueCreate(
        LOG_QUEUE_LENGTH,
        sizeof(TrafficLogEvent)
    );

    if (log_queue == NULL) {
        fputs("Could not create the logging queue.\n", stderr);
        return 1;
    }

    if (create_traffic_task(
            logging_task,
            "TrafficLogger",
            tskIDLE_PRIORITY + 3
        ) != pdPASS ||
        create_traffic_task(
            traffic_controller_task,
            "TrafficControl",
            tskIDLE_PRIORITY + 2
        ) != pdPASS ||
        create_traffic_task(
            display_task,
            "TrafficDisplay",
            tskIDLE_PRIORITY + 1
        ) != pdPASS) {
        fputs("Could not create traffic tasks.\n", stderr);
        return 1;
    }

    puts("Smart Intersection Traffic Signal Controller");
    puts("Green: 10 seconds | Yellow: 2 seconds");
    puts("Current status displayed every second.\n");

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}