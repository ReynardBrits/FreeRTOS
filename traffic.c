#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#define GREEN_DURATION_MS 5000
#define YELLOW_DURATION_MS 2000
#define RED_DURATION_MS 7000

static void display_ns_state(const char *state)
{
    taskENTER_CRITICAL();

    printf("[NS] %s\n", state);

    taskEXIT_CRITICAL();
}

static void north_south_task(void *argument)
{
    (void)argument;

    for (;;) {
        display_ns_state("GREEN");
        vTaskDelay(pdMS_TO_TICKS(GREEN_DURATION_MS));

        display_ns_state("YELLOW");
        vTaskDelay(pdMS_TO_TICKS(YELLOW_DURATION_MS));

        display_ns_state("RED");
        vTaskDelay(pdMS_TO_TICKS(RED_DURATION_MS));
    }
}

int main(void)
{
    BaseType_t result = xTaskCreate(
        north_south_task,
        "NorthSouth",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    if (result != pdPASS) {
        fputs("Could not create the North-South task.\n", stderr);
        return 1;
    }

    puts("Question 3.1: North-South Traffic Light Timing");
    puts("Green: 5 seconds | Yellow: 2 seconds | Red: 7 seconds\n");

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}