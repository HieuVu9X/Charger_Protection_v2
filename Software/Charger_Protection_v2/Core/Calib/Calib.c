#include "Calib.h"

#include <stddef.h>
#include <string.h>
#include "CalibFlash.h"

#define CALIB_MAGIC   0x324c4143U
#define CALIB_VERSION 2U
#define CALIB_COMMIT  0xa55aU

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    CalibParams_t params;
    ProtectionParams_t protection;
    uint32_t crc;
    uint16_t commit;
    uint16_t reserved;
} CalibRecord_t;

/* Read-only compatibility with the original 40-byte Calib record. */
typedef struct
{
    uint32_t magic;
    uint16_t version, size;
    uint32_t sequence;
    CalibParams_t params;
    uint32_t crc;
    uint16_t commit, reserved;
} CalibLegacyRecord_t;

_Static_assert(sizeof(float) == 4 && sizeof(CalibParams_t) == 20 &&
               sizeof(ProtectionParams_t) == 40 &&
               offsetof(ProtectionParams_t, recover_hold_ms) == 36 &&
               offsetof(CalibRecord_t, crc) == 72 &&
               offsetof(CalibRecord_t, commit) == 76 &&
               sizeof(CalibRecord_t) == 80 && sizeof(CalibLegacyRecord_t) == 40,
               "Calib record layout changed: increment storage version");
_Static_assert(CALIB_PAGE_COUNT == 1U && sizeof(CalibRecord_t) <= CALIB_PAGE_SIZE,
               "Calib storage uses exactly one Flash page");

static CalibParams_t active = CALIB_DEFAULT_PARAMS;
static ProtectionParams_t active_protection = PROTECTION_DEFAULT_PARAMS;
static uint8_t active_from_flash, reset_required;

uint8_t Calib_Validate(const CalibParams_t *params)
{
    if (params == NULL) return 0U;
    return params->vin_gain >= 0.25f && params->vin_gain <= 4.0f &&
           params->vout_gain >= 0.25f && params->vout_gain <= 4.0f &&
           params->current_gain >= 0.25f && params->current_gain <= 4.0f &&
           params->leakage_gain >= 0.25f && params->leakage_gain <= 4.0f &&
           params->temperature_offset_c >= -20.0f &&
           params->temperature_offset_c <= 20.0f;
}

uint8_t Calib_ValidateProtection(const ProtectionParams_t *p)
{
    if (p == NULL) return 0U;
    /* Bounded comparisons also reject NaN and infinity without fast-math. */
    return p->uv_in_v >= 1.0f && p->ov_v <= 400.0f &&
        p->uv_in_v < p->uv_in_recover_v &&
        p->uv_in_recover_v < p->ov_recover_v && p->ov_recover_v < p->ov_v &&
        p->oc_a >= 0.01f && p->oc_a <= 30.0f &&
        p->leak_a >= 0.001f && p->leak_a <= 1.0f &&
        p->leak_fast_peak_a >= 0.001f && p->leak_fast_peak_a <= 1.0f &&
        p->ot_recover_c >= -40.0f && p->ot_c <= 150.0f &&
        p->ot_recover_c < p->ot_c &&
        p->recover_hold_ms >= 1U && p->recover_hold_ms <= UINT16_MAX;
}

/* CRC-32/ISO-HDLC, polynomial 0x04c11db7, reflected. */
static uint32_t RecordCrc(const void *data, uint32_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xffffffffU;
    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static uint8_t RecordValid(const CalibRecord_t *record)
{
    return record->magic == CALIB_MAGIC &&
           record->version == CALIB_VERSION &&
           record->size == sizeof(*record) &&
           record->commit == CALIB_COMMIT &&
           Calib_Validate(&record->params) &&
           Calib_ValidateProtection(&record->protection) &&
           record->crc == RecordCrc(record, offsetof(CalibRecord_t, crc));
}

/* Normalize either valid format in RAM. No Flash writes at boot/read time. */
static uint8_t ReadRecord(CalibRecord_t *record)
{
    const void *page = CalibFlash_Page(0U);
    memcpy(record, page, sizeof(*record));
    if (RecordValid(record)) return 2U;
    const CalibLegacyRecord_t *legacy = page;
    if (legacy->magic != CALIB_MAGIC || legacy->version != 1U ||
        legacy->size != sizeof(*legacy) || legacy->commit != CALIB_COMMIT ||
        !Calib_Validate(&legacy->params) ||
        legacy->crc != RecordCrc(legacy, offsetof(CalibLegacyRecord_t, crc)))
        return 0U;
    record->params = legacy->params;
    record->sequence = legacy->sequence;
    record->protection = (ProtectionParams_t)PROTECTION_DEFAULT_PARAMS;
    return 1U;
}

uint8_t Calib_Init(void)
{
    CalibRecord_t record;
    active = (CalibParams_t)CALIB_DEFAULT_PARAMS;
    active_protection = (ProtectionParams_t)PROTECTION_DEFAULT_PARAMS;
    active_from_flash = reset_required = 0U;
    if (!ReadRecord(&record)) return 0U;
    active = record.params;
    active_protection = record.protection;
    active_from_flash = 1U;
    return 1U;
}

const CalibParams_t *Calib_Get(void)
{
    return &active;
}

const ProtectionParams_t *Calib_GetProtection(void)
{
    return &active_protection;
}

void Calib_GetInfo(CalibInfo_t *info)
{
    CalibRecord_t record;
    info->stored_valid = ReadRecord(&record) != 0U;
    info->sequence = info->stored_valid ? record.sequence : 0U;
    info->active_from_flash = active_from_flash;
    info->reset_required = reset_required;
}

CalibStatus_t Calib_ReadStored(CalibParams_t *params)
{
    CalibRecord_t record;
    if (params == NULL || !ReadRecord(&record)) return CALIB_INVALID;
    *params = record.params;
    return CALIB_OK;
}

CalibStatus_t Calib_RestoreDefaults(void)
{
    const CalibParams_t defaults = CALIB_DEFAULT_PARAMS;
    return Calib_Save(&defaults);
}

CalibStatus_t Calib_ReadStoredProtection(ProtectionParams_t *params)
{
    CalibRecord_t record;
    if (params == NULL || ReadRecord(&record) != 2U) return CALIB_INVALID;
    *params = record.protection;
    return CALIB_OK;
}

static CalibStatus_t Save(const CalibParams_t *params,
                         const ProtectionParams_t *protection)
{
    if ((params && !Calib_Validate(params)) ||
        (protection && !Calib_ValidateProtection(protection))) return CALIB_INVALID;
    CalibRecord_t record;
    uint8_t version = ReadRecord(&record);
    if (version)
    {
        if ((!params || memcmp(&record.params, params, sizeof(*params)) == 0) &&
            (!protection || (version == 2U &&
             memcmp(&record.protection, protection, sizeof(*protection)) == 0)))
            return CALIB_UNCHANGED;
        record.sequence++;
    }
    else
    {
        record.sequence = 0U;
        record.params = (CalibParams_t)CALIB_DEFAULT_PARAMS;
        record.protection = (ProtectionParams_t)PROTECTION_DEFAULT_PARAMS;
    }
    record.magic = CALIB_MAGIC; record.version = CALIB_VERSION;
    record.size = sizeof(record); record.commit = CALIB_COMMIT;
    record.reserved = 0xffffU;
    if (params) record.params = *params;
    if (protection) record.protection = *protection;
    record.crc = RecordCrc(&record, offsetof(CalibRecord_t, crc));
    CalibStatus_t status = CalibFlash_Replace(0U, &record,
        offsetof(CalibRecord_t, commit), CALIB_COMMIT);
    /* The Flash port enters service before any real erase/program attempt. */
    if (status == CALIB_OK || status == CALIB_FLASH_ERROR) reset_required = 1U;
    if (status != CALIB_OK) return status;
    const CalibRecord_t *saved = CalibFlash_Page(0U);
    return RecordValid(saved) && memcmp(saved, &record,
        offsetof(CalibRecord_t, reserved)) == 0
        ? CALIB_OK : CALIB_FLASH_ERROR;
}

CalibStatus_t Calib_Save(const CalibParams_t *params)
{
    return params ? Save(params, NULL) : CALIB_INVALID;
}

CalibStatus_t Calib_SaveProtection(const ProtectionParams_t *params)
{
    return params ? Save(NULL, params) : CALIB_INVALID;
}

CalibStatus_t Calib_RestoreProtectionDefaults(void)
{
    const ProtectionParams_t defaults = PROTECTION_DEFAULT_PARAMS;
    return Calib_SaveProtection(&defaults);
}
