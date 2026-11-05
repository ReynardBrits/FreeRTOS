#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

static void test_task(void *argument)
{
    (void)argument;

    for (;;) {
        taskENTER_CRITICAL();
        puts("FreeRTOS task is running.");
        taskEXIT_CRITICAL();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    if (xTaskCreate(test_task, "Test", configMINIMAL_STACK_SIZE,
                    NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        fputs("Could not create the test task.\n", stderr);
        return 1;
    }

    vTaskStartScheduler();

    fputs("The scheduler failed to start.\n", stderr);
    return 1;
}