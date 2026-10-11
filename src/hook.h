#ifndef DS45PAD_HOOK_H
#define DS45PAD_HOOK_H

#include <stdint.h>

/* NIDs are SHA-1(name + Sony's 16 byte suffix), first four bytes read little
 * endian. The values below were generated with that algorithm and the
 * algorithm itself checked against three NIDs whose values are already known
 * from published plugins (allocator/malloc = 0x759E0635,
 * allocator/free = 0x77A602DD, sys_io/cellPadInit = 0x1CF98800). */
#define NID_CELL_PAD_SET_ACT_DIRECT   0xF65544EEu
#define NID_CELL_PAD_GET_DATA         0x8B72CDA1u
#define NID_CELL_PAD_GET_DATA_EXTRA   0x6BC09C61u
#define NID_CELL_PAD_GET_INFO2        0xA703A51Du
#define NID_CELL_PAD_GET_CAPABILITY   0xDBF4C59Cu
#define NID_CELL_PAD_PERIPH_GET_INFO  0x4CC9B68Du
#define NID_VSHTASK_NOTIFY            0xA02D46E7u

typedef struct {
	uint32_t *slot;   /* import table entry we overwrote      */
	uint32_t  saved;  /* original function descriptor address */
} ds45_detour_t;

/* A detour on the shared function descriptor: affects every module in the
 * process, not just this one. `saved` doubles as a callable descriptor for
 * reaching the original. */
typedef struct {
	uint32_t *opd;
	uint32_t  saved[2];
} ds45_opd_detour_t;

/* `target` is the imported function itself, e.g. ds45_detour_opd(cellPadGetData,
 * my_hook, &d). No NID and no table walk involved. */
int  ds45_detour_opd(void *target, void *replacement, ds45_opd_detour_t *d);
void ds45_detour_opd_remove(ds45_opd_detour_t *d);

/* Locate the import table entry for `fnid` in `module`, or NULL.
 * Walks the process ELF, so it only resolves inside the VSH. */
uint32_t *ds45_find_slot(const char *module, uint32_t fnid);

/* Resolve an imported function to a callable pointer, or NULL. */
void *ds45_resolve(const char *module, uint32_t fnid);

/* Redirect an imported function to `replacement`. Returns 0 on success. */
int  ds45_detour(const char *module, uint32_t fnid, void *replacement,
                 ds45_detour_t *d);
void ds45_detour_remove(ds45_detour_t *d);

#endif /* DS45PAD_HOOK_H */
