### Task: Simple Rocket Flight Controller Demo using Zephyr RTOS

- **Goal:**
  Develop a simple rocket flight controller demonstration using **Zephyr RTOS**, an **Olimex STM32-H407** development board, and a **BMP280** pressure and temperature sensor. The system should demonstrate concurrent task execution, non-binding inter-task communication through shared memory, basic flight-state detection, and priority-based LED control.

- **Requirements:**
  1. **Sensor Task:** Periodically read pressure and temperature from the BMP280 and store both values in thread-safe shared memory. Other tasks must be able to access the latest measurements independently, without direct task-to-task dependencies.
  2. **Altitude Task:** Read pressure from shared memory, calculate the corresponding barometric altitude, and publish the calculated height to a separate shared-memory variable.
  3. **Apogee Detection Task:** Monitor the calculated altitude and its rate of change. When the altitude exceeds a configurable minimum threshold and the average vertical velocity over the previous 750 miliseconds becomes negative (indicating constant descent), trigger a simulated parachute deployment by turning the user LED on continuously for 10 seconds. Deployment should trigger only once per simulated flight.
  4. **Temperature Monitoring Task:** Continuously monitor the temperature from shared memory. If it exceeds a configurable threshold, trigger an overheating alarm by blinking the same user LED with a **500 ms period** for 5 seconds.
  5. **LED Priority Management:** The temperature alarm must have higher priority than parachute deployment. While overheating is detected, the LED must blink regardless of whether parachute deployment is active. When the temperature returns to normal, LED control should revert to the parachute indication if its original 10-second interval has not expired.
  6. **Concurrency:** Implement the functionality using separate Zephyr threads, synchronized shared-memory access, and appropriate RTOS timing mechanisms. Shared data must remain accessible to multiple independent consumers without requiring direct communication between tasks.

- **Definition of Done:**
  1. **Working Demo:** The application successfully builds, flashes, and runs on the Olimex STM32-H407 with the BMP280 connected.
  2. **Functional Verification:** Demonstrate live sensor readings, altitude calculations, apogee detection, the 10-second parachute LED indication, the temperature alarm, and correct LED priority when both conditions are active. Thresholds may be adjusted to make the scenarios reproducible during testing.
  3. **Documentation:** Provide a README containing hardware connections, Zephyr environment setup, an overview of the task architecture and shared-memory design, and the altitude and descent-detection algorithms.
  4. **Deliverables:** Source code, Zephyr configuration and Devicetree files, documentation, and a working demonstration (pre-recorded) confirming the required behavior.
