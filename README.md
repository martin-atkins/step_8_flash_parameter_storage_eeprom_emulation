# STM32 Training — Step 10: Flash / EEPROM Emulation (NUCLEO-F411RE)

Persistent key/value storage on an STM32F4, which has **no on-chip EEPROM**. Values
are kept in flash and survive resets and power cycles, using a two-sector
ping-pong scheme that is **wear-levelled** and **power-fail safe**.

This is the final hands-on step of the STM32 training track:

| Step | Topic | Status |
|------|-------|--------|
| 1 | GPIO / LED blink | ✅ |
| 2–5 | Timers, UART, interrupts, FreeRTOS | ✅ |
| 6 | ADC + DMA acquisition | ✅ |
| 7 | LoRa RF comms (Nucleo + DX-PJ27) | ✅ |
| 8 | FreeRTOS scheduling/queues/semaphores | ✅ (done in the LoRa node project) |
| 9 | Low-power modes (STOP/STANDBY, RTC wakeup) | ✅ (done via DEV-568) |
| **10** | **Flash / EEPROM emulation** | **← this project** |
| 11 | USB / advanced comms | optional stretch |

## Why emulation (and why this is interview-worthy)

The STM32F4 can only erase flash a whole **sector** at a time, and a sector can
only be erased a finite number of times (~10k cycles). Naively erase-and-rewrite
one sector on every save and you'd (a) lose all data for the duration of the
erase — a power glitch there corrupts everything — and (b) wear the sector out.

This driver solves both:

- **Append-only log.** A "write" never erases; it appends a new
  `[16-bit data][16-bit virtual address]` record. The newest record for an
  address wins, so a read scans backwards and returns the first match.
- **Two-page ping-pong.** Two sectors act as pages. When the active page fills,
  the latest value of every variable is copied to the spare page, the spare is
  marked `VALID`, and only then is the old page erased. Erases are spread across
  both sectors → roughly double the effective endurance.
- **Power-fail safety.** The header status transitions
  `ERASED → RECEIVE_DATA → VALID → (erase old)` mean that whatever instant power
  is lost, `EE_Init()` at the next boot can tell which page is authoritative and
  finish or roll back the transfer. No torn writes.

## Flash map used (F411RE, 512 KB)

| Sector | Address | Size | Use |
|--------|---------|------|-----|
| 0–3 | `0x08000000`+ | 16 KB ea | program code |
| 4 | `0x08010000` | 64 KB | program code |
| 5 | `0x08020000` | 128 KB | free |
| **6** | **`0x08040000`** | **128 KB** | **EEPROM page A** |
| **7** | **`0x08060000`** | **128 KB** | **EEPROM page B** |

Sectors 6 & 7 are the last two, well clear of a small training program, so **no
linker-script changes are needed**. If your application ever grows past
`0x08040000`, reserve these sectors in the linker `.ld` file.

## Files

```
Core/Inc/eeprom.h            EEPROM driver public API + flash geometry
Core/Src/eeprom.c            two-page emulation implementation
Core/Inc/app_eeprom_demo.h   demo entry points
Core/Src/app_eeprom_demo.c   boot-counter demo (prints over USART2)
```

## API

```c
uint16_t EE_Init(void);                                  // call once at boot (flash unlocked)
uint16_t EE_Format(void);                                // wipe both pages
uint16_t EE_ReadVariable(uint16_t virtAddr, uint16_t *data);
uint16_t EE_WriteVariable(uint16_t virtAddr, uint16_t data);
```

- Values are **16-bit**. Store a 32-bit value across two virtual addresses
  (e.g. `0x0010` = low half, `0x0011` = high half); for blobs, use a run of
  addresses. Virtual address `0xFFFF` is illegal (it collides with erased flash).
- `EE_ReadVariable` returns `EE_VAR_NOT_FOUND` for an address never written —
  the caller supplies the default (the demo treats it as 0).

## Integrating into a CubeIDE project

1. Generate a project for **NUCLEO-F411RE** in STM32CubeMX/CubeIDE with **USART2**
   enabled (default PA2/PA3, 115200 8N1 — it's wired to the ST-LINK VCP).
2. Drop `eeprom.c` / `app_eeprom_demo.c` into `Core/Src` and the headers into
   `Core/Inc` (already laid out that way here).
3. In `main.c`:

   ```c
   /* USER CODE BEGIN Includes */
   #include "app_eeprom_demo.h"
   /* USER CODE END Includes */

   /* after MX_USART2_UART_Init(): */
   /* USER CODE BEGIN 2 */
   EEPROM_Demo_Init();
   EEPROM_Demo_Run();
   /* USER CODE END 2 */
   ```

4. Build, flash, open a serial terminal (PuTTY / `pio device monitor` / Tera Term)
   on the ST-LINK COM port at **115200 8N1**.

## Verifying it works

1. Flash and reset → terminal shows `Boot count: 1`.
2. Press the black **RESET** button (or power-cycle) → `Boot count: 2`, `3`, …
   The count climbing across a **power cycle** (not just reset) is the proof the
   value is in flash, not RAM.
3. To prove wear-levelling/transfer, loop `EE_WriteVariable` thousands of times
   and watch (via debugger) the `VALID_PAGE` header migrate from sector 6 to 7
   and back as pages fill and transfer.
4. To start clean, call `EE_Format()` once (or mass-erase from the programmer).                                                       111 +4. To start clean: **hold the blue USER button (B1) while pressing RESET** — the
   demo calls `EE_Format()` and the count restarts at 1. (Equivalently, call
   `EE_Format()` from code, or mass-erase sectors 6 & 7 from the programmer.)

## Talking points for an interview

- Difference between **byte-erasable EEPROM (STM32L0/L1)** and **sector-erase
  flash (F4)**, and why the latter needs emulation.
- **Wear-levelling** and flash **endurance** (~10k erase cycles/sector) — and how
  the two-page swap multiplies it.
- **Power-fail atomicity**: the `RECEIVE_DATA → VALID → erase` ordering and how
  `EE_Init()` recovers an interrupted transfer.
- Why writes are **half-word programmed** and why `FLASH_VOLTAGE_RANGE_3` is the
  correct choice at the Nucleo's 3.3 V.
- Trade-offs vs. ST's **X-CUBE-EEPROM** package and an external **I²C/SPI FRAM**
  (unlimited endurance, byte-writable, but extra BOM cost and a bus).

## Credits / lineage

The two-page algorithm follows the well-established ST EEPROM-emulation app-note
approach (AN3969 / AN4061 family), reimplemented cleanly here for the F4 HAL.
