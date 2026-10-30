#pragma once

/*
 * Replay mode: publishes a recorded trace into the measurement store at the
 * same 50 ms cadence as the sensor, so the altitude, apogee, heat and LED
 * threads run unchanged. Only the source of the samples differs.
 *
 * Exactly one publisher exists per boot: main() starts either the sensor
 * thread or this one, never both.
 */
void replay_task_start();

/* Name of the trace compiled into this build. */
const char *replay_trace_name();
