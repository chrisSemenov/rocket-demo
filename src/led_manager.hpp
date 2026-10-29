#pragma once

/*
 * Starts the LED manager, which is the only writer of the user LED.
 * Returns false if the LED cannot be configured, so startup can report it
 * instead of silently blinking nothing.
 */
bool led_manager_start();
