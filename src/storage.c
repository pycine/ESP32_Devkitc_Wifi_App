#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h> /* Required header for the littlefs state structure */
#include <zephyr/storage/flash_map.h>
#include "storage.h"

#define STORAGE_PARTITION_NODE DT_NODELABEL(storage_partition)

/* Declare the mandatory LittleFS backend cache structure */
static struct fs_littlefs lfs_storage_data;

static struct fs_mount_t storage_mnt = {
    .type = FS_LITTLEFS,
    .mnt_point = "/storage",
    .storage_dev = (void *)DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE),
    /* Fix: Pass the address of the allocated state block instead of NULL */
    .fs_data = &lfs_storage_data,
};

int storage_init(void)
{
    int ret = fs_mount(&storage_mnt);
    if (ret < 0 && ret != -EALREADY) {
        printk("Failed to mount LittleFS (%d)\n", ret);
        return ret;
    }
    return 0;
}

int storage_save(const char *filename, const void *data, size_t len)
{
    struct fs_file_t file;
    char path[64]; /* Explicitly allocated size array bounds */
    int ret;

    snprintk(path, sizeof(path), "/storage/%s", filename);
    fs_file_t_init(&file);

    ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
    if (ret < 0) return ret;

    ret = fs_write(&file, data, len);
    fs_close(&file);

    return ret; 
}

int storage_load(const char *filename, void *data, size_t len)
{
    struct fs_file_t file;
    char path[64]; /* Explicitly allocated size array bounds */
    int ret;

    snprintk(path, sizeof(path), "/storage/%s", filename);
    fs_file_t_init(&file);

    ret = fs_open(&file, path, FS_O_READ);
    if (ret < 0) return ret; 

    ret = fs_read(&file, data, len);
    fs_close(&file);

    return ret; 
}


int storage_save_u64(const char *filename, uint64_t value) {
    char buf[32];
    snprintk(buf, sizeof(buf), "%llu", value);
    return storage_save(filename, buf, strlen(buf));
}

int storage_load_u64(const char *filename, uint64_t *value) {
    char buf[32];
    int ret = storage_load(filename, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        *value = strtoull(buf, NULL, 10);
        return 0;
    }
    return ret;  // negative on error
}