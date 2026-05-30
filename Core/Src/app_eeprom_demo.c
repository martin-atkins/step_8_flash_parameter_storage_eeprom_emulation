/**
  ******************************************************************************
  * @file    app_eeprom_demo.c
  * @brief   Demonstrates the EEPROM emulation driver with a persistent boot
  *          counter and a couple of extra variables.
  *
  *          Wiring: NUCLEO-F411RE routes USART2 (PA2/PA3) through the on-board
  *          ST-LINK virtual COM port, so the UART output appears on your PC at
  *          115200 8N1 with no extra cabling.
  *
  *          Expected behaviour: each reset prints an incrementing count, and the
  *          value survives power cycles because it lives in flash, not RAM.
  *
  *              Boot count: 1   (lifetime writes: 1)
  *              Boot count: 2   (lifetime writes: 2)
  *              ...
  ******************************************************************************
  */

#include "app_eeprom_demo.h"
#include "eeprom.h"
#include <stdio.h>
#include <string.h>

/* The application's virtual addresses. NB_OF_VAR (in eeprom.h) must match this
 * table's length, and no entry may be 0xFFFF. */
#define VA_BOOT_COUNT    ((uint16_t)0x0001)
#define VA_LAST_RESET    ((uint16_t)0x0002)
#define VA_WRITE_TALLY   ((uint16_t)0x0003)

uint16_t VirtAddVarTab[NB_OF_VAR] = { VA_BOOT_COUNT, VA_LAST_RESET, VA_WRITE_TALLY };

/* Provided by CubeMX-generated code (main.c / usart.c). */
extern UART_HandleTypeDef huart2;

static void uart_print(const char *s)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

void EEPROM_Demo_Init(void)
{
  /* Flash must be unlocked before any program/erase the driver performs. */
  HAL_FLASH_Unlock();

  if (EE_Init() != HAL_OK)
  {
    uart_print("EEPROM init FAILED\r\n");
    /* In production: latch a fault / blink an error code here. */
  }

  /* Re-lock between bursts of writes for safety; the driver unlocks per op?  No -
   * HAL_FLASH_Program assumes an unlocked controller, so we keep it unlocked for
   * the lifetime of the demo. Lock it again if you finish all NV writes. */
}

void EEPROM_Demo_Run(void)
{
  uint16_t bootCount = 0U;
  uint16_t writeTally = 0U;
  char line[80];

  /* First boot on a freshly formatted chip => variable not found => treat as 0. */
  if (EE_ReadVariable(VA_BOOT_COUNT, &bootCount) != EE_OK)
  {
    bootCount = 0U;
  }
  if (EE_ReadVariable(VA_WRITE_TALLY, &writeTally) != EE_OK)
  {
    writeTally = 0U;
  }

  bootCount++;
  writeTally++;

  EE_WriteVariable(VA_BOOT_COUNT, bootCount);
  EE_WriteVariable(VA_WRITE_TALLY, writeTally);

  (void)snprintf(line, sizeof(line),
                 "Boot count: %u   (lifetime writes: %u)\r\n",
                 (unsigned)bootCount, (unsigned)writeTally);
  uart_print(line);
}
