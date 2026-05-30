/**
  ******************************************************************************
  * @file    eeprom.c
  * @brief   Two-page (ping-pong) EEPROM emulation for STM32F4 flash sectors.
  *
  *          Storage model
  *          -------------
  *          Each page is one flash sector. Its first half-word holds a STATUS
  *          (ERASED / RECEIVE_DATA / VALID_PAGE). After the header, data is
  *          appended as 32-bit records, never overwritten in place:
  *
  *              [ 16-bit DATA ][ 16-bit VIRTUAL ADDRESS ]
  *               (Address)       (Address + 2)
  *
  *          A "write" appends a new record. The newest record for a virtual
  *          address wins, so a "read" scans the page from the end backwards
  *          and returns the first match it finds.
  *
  *          When the active page fills up, EE_PageTransfer() copies the latest
  *          value of every variable into the spare page, marks it VALID, then
  *          erases the old page. Because the status transition order is
  *          RECEIVE_DATA -> (copy) -> VALID -> (erase old), a power loss at any
  *          point is recoverable by EE_Init() at the next boot.
  *
  *          IMPORTANT: HAL_FLASH_Unlock() must be called before EE_Init().
  ******************************************************************************
  */

#include "eeprom.h"

/* --- Private helper prototypes --- */
static HAL_StatusTypeDef EE_EraseSector(uint32_t SectorId);
static uint16_t EE_FindValidPage(uint8_t Operation);
static uint16_t EE_VerifyPageFullWriteVariable(uint16_t VirtAddress, uint16_t Data);
static uint16_t EE_PageTransfer(uint16_t VirtAddress, uint16_t Data);
static uint16_t EE_VerifyPageFullyErased(uint32_t Address);

/* ------------------------------------------------------------------------- */
/* Low-level flash helpers                                                   */
/* ------------------------------------------------------------------------- */

/**
  * @brief  Erase a single flash sector. Voltage range 3 (2.7-3.6 V) matches the
  *         Nucleo's 3.3 V supply and permits word/half-word programming.
  */
static HAL_StatusTypeDef EE_EraseSector(uint32_t SectorId)
{
  FLASH_EraseInitTypeDef eraseInit;
  uint32_t sectorError = 0U;

  eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
  eraseInit.Sector       = SectorId;
  eraseInit.NbSectors    = 1U;
  eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  return HAL_FLASHEx_Erase(&eraseInit, &sectorError);
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

/**
  * @brief  Restore the pages to a known good state at boot, recovering from any
  *         power loss that interrupted a previous page transfer.
  * @retval HAL_OK on success, otherwise a flash error / status code.
  */
uint16_t EE_Init(void)
{
  uint16_t pageStatus0, pageStatus1;
  uint16_t varIdx;
  uint16_t eepromStatus, readStatus;
  uint16_t dataVar = 0U;
  int16_t  skipIdx = -1;
  HAL_StatusTypeDef flashStatus;

  /* Clear any error flags pending from a prior reset before touching flash. */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGSERR);

  pageStatus0 = (*(__IO uint16_t *)PAGE0_BASE_ADDRESS);
  pageStatus1 = (*(__IO uint16_t *)PAGE1_BASE_ADDRESS);

  switch (pageStatus0)
  {
    /* ---- Page0 ERASED ------------------------------------------------- */
    case ERASED:
      if (pageStatus1 == VALID_PAGE)            /* normal: data lives on Page1 */
      {
        if (EE_VerifyPageFullyErased(PAGE0_BASE_ADDRESS) == 0U)
        {
          flashStatus = EE_EraseSector(PAGE0_ID);
          if (flashStatus != HAL_OK) { return flashStatus; }
        }
      }
      else if (pageStatus1 == RECEIVE_DATA)     /* transfer to Page1 was interrupted */
      {
        if (EE_VerifyPageFullyErased(PAGE0_BASE_ADDRESS) == 0U)
        {
          flashStatus = EE_EraseSector(PAGE0_ID);
          if (flashStatus != HAL_OK) { return flashStatus; }
        }
        flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, PAGE1_BASE_ADDRESS, VALID_PAGE);
        if (flashStatus != HAL_OK) { return flashStatus; }
      }
      else                                      /* both blank/invalid -> fresh format */
      {
        eepromStatus = EE_Format();
        if (eepromStatus != HAL_OK) { return eepromStatus; }
      }
      break;

    /* ---- Page0 RECEIVE_DATA ------------------------------------------- */
    case RECEIVE_DATA:
      if (pageStatus1 == VALID_PAGE)            /* transfer (Page1 -> Page0) interrupted */
      {
        for (varIdx = 0U; varIdx < NB_OF_VAR; varIdx++)
        {
          /* The variable already copied to the new page (first record) is skipped. */
          if ((*(__IO uint16_t *)(PAGE0_BASE_ADDRESS + 6U)) == VirtAddVarTab[varIdx])
          {
            skipIdx = (int16_t)varIdx;
          }
          if ((int16_t)varIdx != skipIdx)
          {
            readStatus = EE_ReadVariable(VirtAddVarTab[varIdx], &dataVar);
            if (readStatus != EE_VAR_NOT_FOUND)
            {
              eepromStatus = EE_VerifyPageFullWriteVariable(VirtAddVarTab[varIdx], dataVar);
              if (eepromStatus != HAL_OK) { return eepromStatus; }
            }
          }
        }
        flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, PAGE0_BASE_ADDRESS, VALID_PAGE);
        if (flashStatus != HAL_OK) { return flashStatus; }
        flashStatus = EE_EraseSector(PAGE1_ID);
        if (flashStatus != HAL_OK) { return flashStatus; }
      }
      else if (pageStatus1 == ERASED)           /* only Page0 was being prepared */
      {
        if (EE_VerifyPageFullyErased(PAGE1_BASE_ADDRESS) == 0U)
        {
          flashStatus = EE_EraseSector(PAGE1_ID);
          if (flashStatus != HAL_OK) { return flashStatus; }
        }
        flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, PAGE0_BASE_ADDRESS, VALID_PAGE);
        if (flashStatus != HAL_OK) { return flashStatus; }
      }
      else
      {
        eepromStatus = EE_Format();
        if (eepromStatus != HAL_OK) { return eepromStatus; }
      }
      break;

    /* ---- Page0 VALID_PAGE --------------------------------------------- */
    case VALID_PAGE:
      if (pageStatus1 == VALID_PAGE)            /* illegal: two valid pages -> format */
      {
        eepromStatus = EE_Format();
        if (eepromStatus != HAL_OK) { return eepromStatus; }
      }
      else if (pageStatus1 == ERASED)           /* normal: data lives on Page0 */
      {
        if (EE_VerifyPageFullyErased(PAGE1_BASE_ADDRESS) == 0U)
        {
          flashStatus = EE_EraseSector(PAGE1_ID);
          if (flashStatus != HAL_OK) { return flashStatus; }
        }
      }
      else                                      /* transfer (Page0 -> Page1) interrupted */
      {
        for (varIdx = 0U; varIdx < NB_OF_VAR; varIdx++)
        {
          if ((*(__IO uint16_t *)(PAGE1_BASE_ADDRESS + 6U)) == VirtAddVarTab[varIdx])
          {
            skipIdx = (int16_t)varIdx;
          }
          if ((int16_t)varIdx != skipIdx)
          {
            readStatus = EE_ReadVariable(VirtAddVarTab[varIdx], &dataVar);
            if (readStatus != EE_VAR_NOT_FOUND)
            {
              eepromStatus = EE_VerifyPageFullWriteVariable(VirtAddVarTab[varIdx], dataVar);
              if (eepromStatus != HAL_OK) { return eepromStatus; }
            }
          }
        }
        flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, PAGE1_BASE_ADDRESS, VALID_PAGE);
        if (flashStatus != HAL_OK) { return flashStatus; }
        flashStatus = EE_EraseSector(PAGE0_ID);
        if (flashStatus != HAL_OK) { return flashStatus; }
      }
      break;

    /* ---- Anything else: garbage in the header -> format --------------- */
    default:
      eepromStatus = EE_Format();
      if (eepromStatus != HAL_OK) { return eepromStatus; }
      break;
  }

  return HAL_OK;
}

/**
  * @brief  Erase both pages and mark Page0 VALID. Destroys all stored data.
  */
uint16_t EE_Format(void)
{
  HAL_StatusTypeDef flashStatus;

  flashStatus = EE_EraseSector(PAGE0_ID);
  if (flashStatus != HAL_OK) { return flashStatus; }

  flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, PAGE0_BASE_ADDRESS, VALID_PAGE);
  if (flashStatus != HAL_OK) { return flashStatus; }

  flashStatus = EE_EraseSector(PAGE1_ID);
  return (uint16_t)flashStatus;
}

/**
  * @brief  Read the most recent value stored for a virtual address.
  * @param  VirtAddress  variable id (must appear in VirtAddVarTab, != 0xFFFF)
  * @param  Data         out: latest value
  * @retval EE_OK, EE_VAR_NOT_FOUND, or NO_VALID_PAGE
  */
uint16_t EE_ReadVariable(uint16_t VirtAddress, uint16_t *Data)
{
  uint16_t validPage;
  uint16_t addressValue;
  uint16_t readStatus = EE_VAR_NOT_FOUND;
  uint32_t address, pageStartAddress;

  validPage = EE_FindValidPage(READ_FROM_VALID_PAGE);
  if (validPage == NO_VALID_PAGE) { return NO_VALID_PAGE; }

  pageStartAddress = EEPROM_START_ADDRESS + ((uint32_t)validPage * PAGE_SIZE);
  /* Start at the page's last half-word and walk back one record (4 bytes) at a time. */
  address = pageStartAddress + PAGE_SIZE - 2U;

  while (address > (pageStartAddress + 2U))
  {
    addressValue = (*(__IO uint16_t *)address);     /* this slot's virtual address */
    if (addressValue == VirtAddress)
    {
      *Data = (*(__IO uint16_t *)(address - 2U));   /* the data sits just below it */
      readStatus = EE_OK;
      break;
    }
    address -= 4U;
  }

  return readStatus;
}

/**
  * @brief  Persist a value for a virtual address (appends a new record).
  * @retval EE_OK, NO_VALID_PAGE, or a flash error code.
  */
uint16_t EE_WriteVariable(uint16_t VirtAddress, uint16_t Data)
{
  uint16_t status;

  status = EE_VerifyPageFullWriteVariable(VirtAddress, Data);
  if (status == PAGE_FULL)
  {
    /* No room: swap to the spare page (carrying all live values over). */
    status = EE_PageTransfer(VirtAddress, Data);
  }

  return status;
}

/* ------------------------------------------------------------------------- */
/* Private helpers                                                           */
/* ------------------------------------------------------------------------- */

/**
  * @brief  Return the page index to use for a read or a write, based on the two
  *         header statuses. Handles the mid-transfer (RECEIVE_DATA) case.
  */
static uint16_t EE_FindValidPage(uint8_t Operation)
{
  uint16_t pageStatus0 = (*(__IO uint16_t *)PAGE0_BASE_ADDRESS);
  uint16_t pageStatus1 = (*(__IO uint16_t *)PAGE1_BASE_ADDRESS);

  switch (Operation)
  {
    case WRITE_IN_VALID_PAGE:    /* prefer the page that is RECEIVE_DATA mid-transfer */
      if (pageStatus1 == VALID_PAGE)
      {
        return (pageStatus0 == RECEIVE_DATA) ? PAGE0 : PAGE1;
      }
      else if (pageStatus0 == VALID_PAGE)
      {
        return (pageStatus1 == RECEIVE_DATA) ? PAGE1 : PAGE0;
      }
      return NO_VALID_PAGE;

    case READ_FROM_VALID_PAGE:
      if (pageStatus0 == VALID_PAGE) { return PAGE0; }
      if (pageStatus1 == VALID_PAGE) { return PAGE1; }
      return NO_VALID_PAGE;

    default:
      return PAGE0;
  }
}

/**
  * @brief  Append one (Data, VirtAddress) record into the first free slot of the
  *         active page. Data is written first, then the virtual address: the
  *         record is only "valid" once the address half-word is committed.
  * @retval EE_OK, PAGE_FULL, NO_VALID_PAGE, or a flash error code.
  */
static uint16_t EE_VerifyPageFullWriteVariable(uint16_t VirtAddress, uint16_t Data)
{
  HAL_StatusTypeDef flashStatus;
  uint16_t validPage;
  uint32_t address, pageEndAddress;

  validPage = EE_FindValidPage(WRITE_IN_VALID_PAGE);
  if (validPage == NO_VALID_PAGE) { return NO_VALID_PAGE; }

  address        = EEPROM_START_ADDRESS + ((uint32_t)validPage * PAGE_SIZE) + 4U;
  pageEndAddress = EEPROM_START_ADDRESS + ((uint32_t)validPage * PAGE_SIZE) + (PAGE_SIZE - 2U);

  /* Scan forward for the first record-sized hole (both half-words still erased). */
  while (address < pageEndAddress)
  {
    if ((*(__IO uint32_t *)address) == 0xFFFFFFFFU)
    {
      flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, Data);
      if (flashStatus != HAL_OK) { return (uint16_t)flashStatus; }

      flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + 2U, VirtAddress);
      return (uint16_t)flashStatus;
    }
    address += 4U;
  }

  return PAGE_FULL;
}

/**
  * @brief  Swap to the spare page: mark it RECEIVE_DATA, write the new value plus
  *         the latest value of every other variable, mark it VALID, erase the old
  *         page. The strict ordering is what makes this power-fail safe.
  */
static uint16_t EE_PageTransfer(uint16_t VirtAddress, uint16_t Data)
{
  HAL_StatusTypeDef flashStatus;
  uint32_t newPageAddress;
  uint16_t oldPageId;
  uint16_t validPage, varIdx;
  uint16_t eepromStatus, readStatus;
  uint16_t dataVar = 0U;

  validPage = EE_FindValidPage(READ_FROM_VALID_PAGE);
  if (validPage == PAGE1)
  {
    newPageAddress = PAGE0_BASE_ADDRESS;
    oldPageId      = PAGE1_ID;
  }
  else if (validPage == PAGE0)
  {
    newPageAddress = PAGE1_BASE_ADDRESS;
    oldPageId      = PAGE0_ID;
  }
  else
  {
    return NO_VALID_PAGE;
  }

  /* 1. New page enters RECEIVE_DATA. */
  flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, newPageAddress, RECEIVE_DATA);
  if (flashStatus != HAL_OK) { return (uint16_t)flashStatus; }

  /* 2. Write the value that triggered the transfer (first record on the new page). */
  eepromStatus = EE_VerifyPageFullWriteVariable(VirtAddress, Data);
  if (eepromStatus != HAL_OK) { return eepromStatus; }

  /* 3. Carry over the latest value of every other variable. */
  for (varIdx = 0U; varIdx < NB_OF_VAR; varIdx++)
  {
    if (VirtAddVarTab[varIdx] != VirtAddress)
    {
      readStatus = EE_ReadVariable(VirtAddVarTab[varIdx], &dataVar);
      if (readStatus != EE_VAR_NOT_FOUND)
      {
        eepromStatus = EE_VerifyPageFullWriteVariable(VirtAddVarTab[varIdx], dataVar);
        if (eepromStatus != HAL_OK) { return eepromStatus; }
      }
    }
  }

  /* 4. Retire the old page. */
  flashStatus = EE_EraseSector(oldPageId);
  if (flashStatus != HAL_OK) { return (uint16_t)flashStatus; }

  /* 5. Promote the new page to VALID (now it is the live data set). */
  flashStatus = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, newPageAddress, VALID_PAGE);
  return (uint16_t)flashStatus;
}

/**
  * @brief  Return 1 if every half-word of the page reads as ERASED, else 0.
  */
static uint16_t EE_VerifyPageFullyErased(uint32_t Address)
{
  uint32_t endAddress = Address + PAGE_SIZE;

  while (Address < endAddress)
  {
    if ((*(__IO uint16_t *)Address) != ERASED)
    {
      return 0U;
    }
    Address += 2U;
  }

  return 1U;
}
