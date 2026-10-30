#pragma once

/*
 * Starts the temperature monitor. It reads the measurement store, runs the
 * heat policy, and publishes a heat request. It never touches the sensor or the
 * LED, and it does not depend on altitude or apogee.
 */
void heat_task_start();
