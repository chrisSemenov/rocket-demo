#pragma once

/*
 * Second, independent reader of the measurement store. It exists to demonstrate
 * that a read does not consume the sample: it runs at a different rate from the
 * telemetry task and must always observe the same sequence. It is replaced by
 * the altitude thread once that lands.
 */
void diag_task_start();
