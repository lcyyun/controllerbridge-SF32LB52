#ifndef TEST_MAPPING_NVDS_H
#define TEST_MAPPING_NVDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NVDS_OK 0U
#define NVDS_FAIL 1U

uint8_t sifli_nvds_flash_adaptor_init(void);
size_t sifli_nvds_flash_read(const char *key, void *value_buf, size_t buf_len);
uint8_t sifli_nvds_flash_write(const char *key, const void *value_buf,
                              size_t buf_len);
uint8_t sifli_nvds_flash_adaptor_delete(const char *key);

void fake_nvds_clear(void);
void fake_nvds_reboot(void);
void fake_nvds_fail_init(bool fail);
void fake_nvds_fail_writes(bool fail);
void fake_nvds_seed(const char *key, const void *data, size_t len);
unsigned int fake_nvds_write_count(void);

#endif
