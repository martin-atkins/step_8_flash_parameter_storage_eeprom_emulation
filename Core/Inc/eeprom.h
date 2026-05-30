/**
  ******************************************************************************
  * @file    eeprom.h
  * @brief   EEPROM emulation driver for STM32F401RE / F411RE (NUCLEO-F4xxRE).
  *
  *          The STM32F4 has no on-chip EEPROM, so non-volatile key/value
  *          storage is emulated across the last two flash sectors of the
  *          512 KB device (sectors 6 & 7). A two-page ping-pong scheme gives:
  *            - power-fail safety (a half-finished write never corrupts data)
  *            - wear-levelling (erases are spread, endurance is multiplied)
  *
  *          Public API:
  *            EE_Init()           - call once at boot (flash must be unlocked)
  *            EE_Format()         - wipe + re-initialise both pages
  *            EE_ReadVariable()   - read latest value of a virtual address
  *            EE_WriteVariable()  - persist a value to a virtual address
  *
  *          Values are 16-bit. To store wider data, split it across several
  *          virtual addresses (see README).
  ******************************************************************************
  */

#ifndef __EEPROM_H
#define __EEPROM_H

#include "stm32f4xx_hal.h"

/* --- Flash geometry: last two sectors of an STM32F401RE/F411RE (512 KB) --- */
#define PAGE0_ID               FLASH_SECTOR_6
#define PAGE1_ID               FLASH_SECTOR_7

#define EEPROM_START_ADDRESS   ((uint32_t)0x08040000U)   /* Sector 6 base       */
#define PAGE_SIZE              ((uint32_t)0x00020000U)    /* 128 KB per sector   */

#define PAGE0_BASE_ADDRESS     ((uint32_t)(EEPROM_START_ADDRESS))
#define PAGE0_END_ADDRESS      ((uint32_t)(EEPROM_START_ADDRESS + (PAGE_SIZE - 1U)))
#define PAGE1_BASE_ADDRESS     ((uint32_t)(EEPROM_START_ADDRESS + PAGE_SIZE))
#define PAGE1_END_ADDRESS      ((uint32_t)(EEPROM_START_ADDRESS + (2U * PAGE_SIZE - 1U)))

#define PAGE0                  ((uint16_t)0x0000)
#define PAGE1                  ((uint16_t)0x0001)

/* --- Page header status values (written into the first half-word of a page) --- */
#define ERASED                 ((uint16_t)0xFFFF)   /* page is empty                       */
#define RECEIVE_DATA           ((uint16_t)0xEEEE)   /* page is receiving data mid-transfer */
#define VALID_PAGE             ((uint16_t)0x0000)   /* page holds the live data set        */

/* --- Operation selector for the page lookup helper --- */
#define READ_FROM_VALID_PAGE   ((uint8_t)0x00)
#define WRITE_IN_VALID_PAGE    ((uint8_t)0x01)

/* --- Status / error codes --- */
#define PAGE_FULL              ((uint16_t)0x0080)   /* no free slot in active page         */
#define NO_VALID_PAGE          ((uint16_t)0x00AB)   /* neither page is VALID               */
#define EE_OK                  ((uint16_t)HAL_OK)
#define EE_VAR_NOT_FOUND       ((uint16_t)0x0001)   /* virtual address has never been written */

/* --- Table of virtual addresses the application uses (defined in app file) --- */
/*     NB: a virtual address of 0xFFFF is illegal (collides with erased flash). */
#define NB_OF_VAR              ((uint8_t)0x03)
extern uint16_t VirtAddVarTab[NB_OF_VAR];

/* --- Public API --- */
uint16_t EE_Init(void);
uint16_t EE_Format(void);
uint16_t EE_ReadVariable(uint16_t VirtAddress, uint16_t *Data);
uint16_t EE_WriteVariable(uint16_t VirtAddress, uint16_t Data);

#endif /* __EEPROM_H */
