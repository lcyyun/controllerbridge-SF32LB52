#include "bf0_sibles_nvds.h"

#include <assert.h>
#include <string.h>

typedef struct {
    char key[32];
    uint8_t data[128];
    size_t len;
} record_t;

static record_t records[4];
static bool initialized;
static bool init_failure;
static bool write_failure;
static unsigned int writes;

static record_t *find_record(const char *key, bool create)
{
    size_t i;
    record_t *empty = NULL;

    assert(key != NULL);
    for (i = 0U; i < sizeof(records) / sizeof(records[0]); ++i) {
        if (strcmp(records[i].key, key) == 0) {
            return &records[i];
        }
        if (records[i].key[0] == '\0') {
            empty = &records[i];
        }
    }
    if (!create) {
        return NULL;
    }
    assert(empty != NULL && strlen(key) < sizeof(empty->key));
    strcpy(empty->key, key);
    return empty;
}

void fake_nvds_reboot(void)
{
    /* Retain only the emulated flash records across a cold runtime restart. */
    initialized = false;
    init_failure = false;
    write_failure = false;
    writes = 0U;
}

void fake_nvds_clear(void)
{
    memset(records, 0, sizeof(records));
    fake_nvds_reboot();
}

void fake_nvds_fail_init(bool fail)
{
    init_failure = fail;
}

void fake_nvds_fail_writes(bool fail)
{
    write_failure = fail;
}

void fake_nvds_seed(const char *key, const void *data, size_t len)
{
    record_t *record = find_record(key, true);

    assert(data != NULL && len <= sizeof(record->data));
    memcpy(record->data, data, len);
    record->len = len;
}

unsigned int fake_nvds_write_count(void)
{
    return writes;
}

uint8_t sifli_nvds_flash_adaptor_init(void)
{
    if (init_failure) {
        return NVDS_FAIL;
    }
    initialized = true;
    return NVDS_OK;
}

size_t sifli_nvds_flash_read(const char *key, void *value_buf, size_t buf_len)
{
    record_t *record = find_record(key, false);
    size_t len;

    if (!initialized || record == NULL) {
        return 0U;
    }
    /* FlashDB returns bytes copied, not the full size of a truncated blob. */
    len = record->len < buf_len ? record->len : buf_len;
    memcpy(value_buf, record->data, len);
    return len;
}

uint8_t sifli_nvds_flash_write(const char *key, const void *value_buf,
                              size_t buf_len)
{
    writes++;
    if (!initialized || write_failure) {
        return NVDS_FAIL;
    }
    fake_nvds_seed(key, value_buf, buf_len);
    return NVDS_OK;
}

uint8_t sifli_nvds_flash_adaptor_delete(const char *key)
{
    record_t *record = find_record(key, false);

    /* The SDK adaptor does not propagate fdb_kv_del's error result. */
    if (initialized && !write_failure && record != NULL) {
        memset(record, 0, sizeof(*record));
    }
    return NVDS_OK;
}
