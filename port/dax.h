#ifndef DAX_H
#define DAX_H
#include "poolrad.h"
u16 dax_unpack(u8 *dst, const u8 *src, u16 packed);
int dax_load(farptr var_ptr, farptr var_size, u8 id, const char *ext, const char *name);
int dax_load_native(farptr *out_ptr, u16 *out_size, u8 id, const char *ext, const char *name);
#endif
