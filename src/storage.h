#ifndef STORAGE_H_
#define STORAGE_H_

#include <stddef.h>

int storage_init(void);
int storage_save(const char *filename, const void *data, size_t len);
/* Changed from char *buffer to const void *data */
int storage_load(const char *filename, void *data, size_t len);

#endif /* STORAGE_H_ */
