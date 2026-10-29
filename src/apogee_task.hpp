#pragma once

/*
 * Starts the apogee thread. It reads altitude snapshots, keeps its own 750 ms
 * history, and publishes a deployment request once per flight. It never touches
 * the LED or the sensor.
 */
void apogee_task_start();
