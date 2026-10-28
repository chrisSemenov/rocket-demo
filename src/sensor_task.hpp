#pragma once

/*
 * Starts the acquisition thread. Returns false if the sensor is not ready, in
 * which case no thread is created and no measurement is ever published.
 */
bool sensor_task_start();
