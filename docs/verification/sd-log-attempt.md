# Attempted: run log on the SD card (abandoned)

**Date:** 2026-10-30
**Outcome:** not delivered. The storage was removed from the firmware; nothing in
the current build touches the card.

## What was attempted

The goal was to keep the last five runs on the board's SD card, so a battery test
could be recovered later by plugging the ST-Link in and printing the file over
RTT.

| Piece | Effort |
|---|---|
| SDMMC1 enablement | The board's devicetree has no SD node and Zephyr ships no SDMMC pin definitions for this SoC, so `PC8–PC11` (data), `PC12` (clock) and `PD2` (command) were declared by hand with `STM32_PINMUX(..., AF12)` |
| Filesystem | FatFS is an optional module (`fatfs`, group `fs`); it was added to the workspace manifest allowlist and fetched |
| Firmware | Mount with format-once, `run-NNNN.csv` rotation of five, 10 Hz CSV, one-second flush, boot-time dump over RTT |

## What worked

The pin assignment and clock were correct. With a card inserted, the first boot
reported:

```
sd: card detected, 3911680 sectors of 512 bytes (1910 MB)
sd: mount failed (-5); filesystem may need formatting
```

So the socket, the pinmux and the SDMMC clock all function.

## What failed

1. **The card never mounted.** `-5` (EIO) on the only boot where it was seen.
   A blank card would have been formatted by the firmware, but that path was
   never reached.
2. **The card stopped answering.** Every subsequent boot returned
   `sd: no card (init rc=-5)`, including after reflashes. SD cards commonly latch
   after a warm reset, and any flash is a warm reset; only a full power cycle
   restored it, and not reliably.
3. **The read path faults.** A boot-time dump tripped the MPU inside ST's HAL:

   ```
   ***** MPU FAULT *****
   Data Access Violation  MMFAR 0x20006ce8
   Faulting instruction address: 0x080079e8
   ```

   Symbolised to `HAL_SD_ReadBlocks_IT` (`stm32f4xx_hal_sd.c:977`).

## Why it was dropped

- **Not required.** The assignment asks for acquisition, altitude, apogee
  detection, temperature monitoring and LED priority. Persistence is not in the
  requirements.
- **Driver-level work.** Making this reliable means investigating the STM32
  SDMMC driver's interrupt/DMA path, not application code.
- **Operational cost.** Every attempt needs a physical power cycle because of the
  latch, which makes iteration slow and the result fragile.

## What happens instead

Run evidence comes from the RTT console captures already recorded in
`docs/verification/` (and the LED's own indication). The five-run retention that
the card was meant to provide is not implemented.

If persistence is picked up again, the lower-risk route is internal flash with
LittleFS: in-tree, no external module, and designed for power loss.

## Housekeeping

The FatFS fetch was reverted: the workspace manifest allowlist no longer lists
`fatfs` (backup kept at `zephyrproject/manifest/west.yml.bak`). The fetched
module remains on disk but is unused by this project.
