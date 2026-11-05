#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#define GREEN_DURATION_MS 10000
#define YELLOW_DURATION_MS 2000

typedef enum {
    LIGHT_RED,
    LIGHT_YELLOW,
    LIGHT_GREEN
} LightState;

typedef struct {
    LightState north_south;
    LightState east_west;
} TrafficState;

static TrafficState traffic_state = {
    LIGHT_RED,
    LIGHT_RED
};

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

static void set_lights(
    LightState north_south,
    LightState east_west
)
{
    /* At least one direction must always be red. */
    configASSERT(
        north_south == LIGHT_RED || east_west == LIGHT_RED
    );

    taskENTER_CRITICAL();

    traffic_state.north_south = north_south;
    traffic_state.east_west = east_west;

    printf(
        "[TRAFFIC] NS %s EW %s\n",
        light_name(traffic_state.north_south),
        light_name(traffic_state.east_west)
    );

    taskEXIT_CRITICAL();
}

static void traffic_controller_task(void *argument)
{
    (void)argument;

    for (;;) {
        /* North-South green; East-West red. */
        set_lights(LIGHT_GREEN, LIGHT_RED);
        vTaskDelay(pdMS_TO_TICKS(GREEN_DURATION_MS));

        /* North-South yellow; East-West remains red. */
        set_lights(LIGHT_YELLOW, LIGHT_RED);
        vTaskDelay(pdMS_TO_TICKS(YELLOW_DURATION_MS));

        /* North-South red; East-West green. */
        set_lights(LIGHT_RED, LIGHT_GREEN);
        vTaskDelay(pdMS_TO_TICKS(GREEN_DURATION_MS));

        /* North-South remains red; East-West yellow. */
        set_lights(LIGHT_RED, LIGHT_YELLOW);
        vTaskDelay(pdMS_TO_TICKS(YELLOW_DURATION_MS));
    }
}

int main(void)
{
    BaseType_t result = xTaskCreate(
        traffic_controller_task,
        "TrafficControl",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    if (result != pdPASS) {
        fputs("Could not create the traffic controller task.\n", stderr);
        return 1;
    }

    puts("Question 3.2: Synchronised Traffic Lights");
    puts("Green: 10 seconds | Yellow: 2 seconds\n");

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}