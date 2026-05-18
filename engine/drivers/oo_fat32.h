#pragma once
#include <stdint.h>

/**
 * OO FAT32 - Standard File System Driver
 * 
 * Allows reading and writing files on FAT32 partitions (e.g. USB or NVMe).
 */

typedef struct {
    char     filename[11]; // 8.3 format
    uint8_t  attributes;
    uint32_t first_cluster;
    uint32_t file_size;
} FatDirEntry;

// Initialize the FAT32 driver on a specific block device
int oo_fat32_init(uint32_t controller_type); // 1 = AHCI, 2 = NVMe

// Open a file by path
int oo_fat32_open(const char *path, FatDirEntry *entry);

// Read file content into a buffer
int oo_fat32_read(const FatDirEntry *entry, void *buffer, uint32_t offset, uint32_t size);

// List directory contents
int oo_fat32_list_dir(const char *path, void (*callback)(const char *name));
