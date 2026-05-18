#include "oo_fat32.h"
#include <string.h>

// FAT32 Extended BIOS Parameter Block
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_short;
    uint8_t  media_type;
    uint16_t fat_size_short;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_long;
    
    // FAT32 Extended fields
    uint32_t fat_size_long;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} Fat32BootSector;

static Fat32BootSector g_bs;
static uint32_t g_first_data_sector = 0;

// Fonction bouchon pour simuler la lecture de secteur via AHCI/NVMe
static int read_disk_sector(uint32_t lba, void *buffer) {
    // Ici, on appellerait oo_nvme_read() ou oo_ahci_read()
    return 0; 
}

int oo_fat32_init(uint32_t controller_type) {
    uint8_t sector_buf[512];
    
    // Lire le secteur de boot (LBA 0 ou début de partition)
    if (read_disk_sector(0, sector_buf) != 0) return -1;
    
    memcpy(&g_bs, sector_buf, sizeof(Fat32BootSector));
    
    // Vérification de la signature FAT32 (0xAA55 aux octets 510-511)
    if (sector_buf[510] != 0x55 || sector_buf[511] != 0xAA) {
        return -2; // Pas un disque bootable/valide
    }
    
    // Calcul du premier secteur de données
    uint32_t fat_size = g_bs.fat_size_short != 0 ? g_bs.fat_size_short : g_bs.fat_size_long;
    g_first_data_sector = g_bs.reserved_sectors + (g_bs.fat_count * fat_size);
    
    return 0; // Initialisation réussie
}

int oo_fat32_open(const char *path, FatDirEntry *entry) {
    if (!entry) return -1;
    
    // Ici, il faudrait parcourir le dossier racine (g_bs.root_cluster)
    // et chercher l'entrée correspondant au 'path'.
    
    return -1; // Non implémenté
}

int oo_fat32_read(const FatDirEntry *entry, void *buffer, uint32_t offset, uint32_t size) {
    // Ici, il faudrait suivre la chaîne de clusters dans la FAT
    // et lire les secteurs correspondants.
    
    return -1; // Non implémenté
}
