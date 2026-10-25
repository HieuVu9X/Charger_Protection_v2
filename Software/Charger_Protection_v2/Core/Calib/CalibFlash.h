/* Internal storage port; host tests substitute a simulated Flash. */
#ifndef CALIB_FLASH_H_
#define CALIB_FLASH_H_

#include "Calib.h"

#define CALIB_PAGE_SIZE 1024U
#define CALIB_PAGE_COUNT 1U

const void *CalibFlash_Page(uint32_t page);
/* Erase the single reserved page, verify body, then program commit last.
 * A failed/interrupted erase or write can destroy the previous record. */
CalibStatus_t CalibFlash_Replace(uint32_t page, const void *body,
                                           uint32_t length, uint16_t commit);

#endif
