#include "drivers/ata.h"

struct ata_device ata_primary_master = {0};
struct ata_device ata_primary_slave = {0};

void ata_init(void) {
    ata_primary_master.exists = 0;
    ata_primary_slave.exists = 0;
}

int ata_detect_devices(void) {
    return 0;
}

int ata_identify(struct ata_device *dev) {
    (void)dev;
    return -1;
}

void ata_print_device_info(struct ata_device *dev) {
    (void)dev;
}

uint8_t ata_status_wait(struct ata_device *dev, uint8_t mask, uint8_t value,
                        int timeout_ms) {
    (void)dev;
    (void)mask;
    (void)value;
    (void)timeout_ms;
    return 0;
}

int ata_wait_ready(struct ata_device *dev) {
    (void)dev;
    return -1;
}

int ata_wait_drq(struct ata_device *dev) {
    (void)dev;
    return -1;
}

int ata_read_sectors(struct ata_device *dev, uint64_t lba, uint8_t count,
                     void *buffer) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buffer;
    return -1;
}

int ata_write_sectors(struct ata_device *dev, uint64_t lba, uint8_t count,
                      const void *buffer) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buffer;
    return -1;
}

void ata_400ns_delay(struct ata_device *dev) {
    (void)dev;
}

void ata_select_drive(struct ata_device *dev) {
    (void)dev;
}
