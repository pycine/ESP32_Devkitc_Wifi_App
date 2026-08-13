#ifndef STORAGE_H_
#define STORAGE_H_

#include <stddef.h>

int storage_init(void);
int storage_save(const char *filename, const void *data, size_t len);
/* Changed from char *buffer to const void *data */
int storage_load(const char *filename, void *data, size_t len);
int storage_save_u64(const char *filename, uint64_t value);
int storage_load_u64(const char *filename, uint64_t *value);

#endif /* STORAGE_H_ */
