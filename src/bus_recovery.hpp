#pragma once

/*
 * Result of the boot-time I2C bus recovery that bus_recovery.cpp registers as a
 * POST_KERNEL init step. It runs before the console exists, so main() reports
 * it; a non-zero value means the bus was latched and the recovery attempt
 * failed, in which case the sensor driver will not have come up.
 */
int bus_recovery_status();
