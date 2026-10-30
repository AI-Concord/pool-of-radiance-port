// DAX archives (original unit at 1802, resident): every picture, map, monster and script block.
//
// File layout: a word giving the size of the directory, the directory (9 bytes per block: id byte,
// offset dword relative to the end of the directory, unpacked size word, packed size word), then
// the packed blocks. Packing is run-length: a control byte 00..7E copies the next n+1 bytes, a
// control byte 81..FF repeats the following byte (256-n) times.
#include <stdio.h>
#include "poolrad.h"
#include "rtl.h"
#include "dax.h"

// 1802:050F
u16 dax_unpack(u8 *dst, const u8 *src, u16 packed) {
    u16 in = 0, out = 0;
    do {
        u8 c = src[in];
        if (c <= 0x7e) { memmove(dst + out, src + in + 1, c + 1); in = (u16)(in + c + 2); out = (u16)(out + c + 1); }
        else if (c >= 0x81) { memset(dst + out, src[in + 1], 256 - c); in = (u16)(in + 2); out = (u16)(out + 256 - c); }
        else break;                           // 7F and 80 never occur (the original would hang on them)
    } while (in < packed);
    return out;
}

// 1802:0716  Load block `id` from the archive <game dir><name><ext>. On success *var_ptr is a newly
// allocated block and *var_size its length; if the block is not in the archive both are zero.
// Returns 0 only when the file itself cannot be opened (the original then asks for the disk).
// Its helpers are folded in: 1802:0020 and 1802:0000 round the buffer to 8 bytes for GetMem/FreeMem, 1802:05CD finds
// the entry in the directory, 1802:06E7 closes the files.
int dax_load(farptr var_ptr, farptr var_size, u8 id, const char *ext, const char *name) {
    farptr p; u16 size;
    if (!dax_load_native(&p, &size, id, ext, name)) return 0;
    SETP16(var_size, 0, size); SETP32(var_ptr, 0, p);
    return 1;
}

// The same for native callers: results in C variables instead of Pascal VAR parameters.
int dax_load_native(farptr *out_ptr, u16 *out_size, u8 id, const char *ext, const char *name) {
    char path[160]; u8 e[9]; u16 dirsize, raw = 0, packed = 0; u32 offset = 0; int found = 0;
    snprintf(path, sizeof path, "%s%s%s", pstr(FARPTR(DATA_SEG, 0x547c)), name, ext);
    FILE *f = plat_fopen(path, "rb");
    if (!f) return 0;
    SETG16(0x49ce, 0);                        // result of the original's "does the file exist" probe (Pascal IOResult)
    if (fread(e, 1, 2, f) != 2) { fclose(f); return 0; }
    dirsize = (u16)(e[0] | (e[1] << 8));
    for (u32 pos = 0; pos < dirsize; pos += 9) {
        if (fread(e, 1, 9, f) != 9) break;
        offset = e[1] | (e[2] << 8) | ((u32)e[3] << 16) | ((u32)e[4] << 24);
        raw = (u16)(e[5] | (e[6] << 8)); packed = (u16)(e[7] | (e[8] << 8));
        if (e[0] == id) { found = 1; break; }
    }
    if (!found) { *out_size = 0; *out_ptr = 0; fclose(f); return 1; }
    farptr tmp = rtl_getmem((u16)((packed + 7) & 0xfff8));
    SETG32(0x557e, tmp);                      // scratch pointer global; left dangling afterwards, as in the original
    farptr res = rtl_getmem(raw);
    *out_ptr = res; *out_size = raw;
    fseek(f, (long)dirsize + 2 + offset, SEEK_SET);
    fread(&mem[LIN(tmp)], 1, packed, f);
    fclose(f);
    dax_unpack(&mem[LIN(res)], &mem[LIN(tmp)], packed);
    rtl_freemem(tmp, (u16)((packed + 7) & 0xfff8));
    return 1;
}
