#pragma once

/*
 * Test-only request generator for the LED manager. Compiled and started only
 * when APP_LED_SELFTEST is defined; it never runs in the released firmware.
 *
 * It publishes scripted requests through the same stores the real producers
 * will use, so the tested path is the production path (checklist 10.1).
 */
void led_selftest_start();
