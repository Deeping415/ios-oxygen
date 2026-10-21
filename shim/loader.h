#ifndef LOADER_H
#define LOADER_H
#include <stdint.h>

int loader_probe_payload(uintptr_t ramdisk_addr, uint64_t ramdisk_size);

#endif
