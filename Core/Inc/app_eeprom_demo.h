/**
  ******************************************************************************
  * @file    app_eeprom_demo.h
  * @brief   Boot-counter demo proving EEPROM-emulated persistence across resets.
  ******************************************************************************
  */

#ifndef __APP_EEPROM_DEMO_H
#define __APP_EEPROM_DEMO_H

#include "stm32f4xx_hal.h"

/* Call once after HAL_Init()/clock/UART setup. Unlocks flash and inits EEPROM. */
void EEPROM_Demo_Init(void);

/* Reads the boot counter, increments + stores it, prints the value over UART. */
void EEPROM_Demo_Run(void);

#endif /* __APP_EEPROM_DEMO_H */
