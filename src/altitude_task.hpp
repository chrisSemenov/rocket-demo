#pragma once

/*
 * Starts the altitude thread. It reads pressure from the measurement store,
 * calibrates a baseline, and publishes a separate altitude snapshot. It never
 * touches the sensor or the LED.
 */
void altitude_task_start();
