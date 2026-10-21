#ifndef FDT_H
#define FDT_H
#include <stdint.h>

int fdt_check_header(const void *fdt);
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp);
int fdt_path_offset(const void *fdt, const char *path);

#endif
