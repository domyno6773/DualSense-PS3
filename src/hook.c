/*
 * DS45PAD - import table lookup and detours.
 *
 * A PRX that imports a system library gets an import stub structure holding
 * the library name, a table of NIDs and a matching table of resolved function
 * pointers. On PowerPC64 ELFv1 a "function pointer" is the address of a two
 * word descriptor {entry point, TOC}, so the loader fills that second table
 * with descriptor addresses.
 *
 * Two things fall out of that:
 *
 *   - resolving a function the SDK has no stub for (vshtask's notification
 *     entry point) is a table walk;
 *   - detouring one is a single store: point the slot at our own descriptor
 *     and every call the module makes lands in our code, with the original
 *     still callable through the saved address.
 *
 * The walk starts from the process ELF at 0x10000, which is where both the
 * VSH and a game's EBOOT are loaded, so the same code works in either.
 */

#include <string.h>

#include "ds45pad.h"
#include "hook.h"

/* Offsets inside an import stub structure (0x2C bytes). */
#define STUB_COUNT      0x06  /* uint16, number of entries */
#define STUB_NAME       0x10  /* char *                    */
#define STUB_FNID_TAB   0x14  /* uint32 *                  */
#define STUB_FUNC_TAB   0x18  /* uint32 *, descriptors     */

/* The table base is read from a fixed address inside the process ELF, and the
 * 0x984 displacement is what published plugins use. Both are firmware
 * dependent, so every pointer taken from there is range checked before being
 * dereferenced: on a layout this code was not written for, the walk has to
 * come back empty rather than take the console down with it. */
#define ADDR_MIN 0x00010000u
#define ADDR_MAX 0x10000000u
#define MAX_ENTRIES 512

static int plausible(uint32_t addr)
{
	return addr >= ADDR_MIN && addr < ADDR_MAX;
}

uint32_t *ds45_find_slot(const char *module, uint32_t fnid)
{
	uint32_t table;
	int guard;

	if (!module)
		return NULL;

	if (!plausible(*(uint32_t *)0x1008C))
		return NULL;

	table = (*(uint32_t *)0x1008C) + 0x984;
	if (!plausible(table))
		return NULL;

	for (guard = 0; guard < MAX_ENTRIES; guard++, table += 4) {
		uint32_t stub_addr = *(uint32_t *)table;
		char *stub;
		uint32_t name_a, fnid_a, func_a;
		uint16_t count;
		int i;

		if (stub_addr == 0)
			break;
		if (!plausible(stub_addr))
			continue;

		stub   = (char *)(uintptr_t)stub_addr;
		name_a = *(uint32_t *)(stub + STUB_NAME);
		fnid_a = *(uint32_t *)(stub + STUB_FNID_TAB);
		func_a = *(uint32_t *)(stub + STUB_FUNC_TAB);
		count  = *(uint16_t *)(stub + STUB_COUNT);

		if (!plausible(name_a) || !plausible(fnid_a) ||
		    !plausible(func_a) || count == 0 || count > 4096)
			continue;

		if (strncmp(module, (const char *)(uintptr_t)name_a,
		            strlen(module) + 1) != 0)
			continue;

		for (i = 0; i < count; i++)
			if (((uint32_t *)(uintptr_t)fnid_a)[i] == fnid)
				return &((uint32_t *)(uintptr_t)func_a)[i];
	}

	return NULL;
}

void *ds45_resolve(const char *module, uint32_t fnid)
{
	uint32_t *slot = ds45_find_slot(module, fnid);

	return slot ? (void *)(uintptr_t)*slot : NULL;
}

int ds45_detour(const char *module, uint32_t fnid, void *replacement,
                ds45_detour_t *d)
{
	uint32_t *slot = ds45_find_slot(module, fnid);

	if (!slot || !replacement)
		return -1;

	d->slot  = slot;
	d->saved = *slot;

	/* `replacement` is already a descriptor address, which is exactly what
	 * the slot is supposed to hold - no trampoline needed, and the TOC comes
	 * along for free. */
	*slot = (uint32_t)(uintptr_t)replacement;

	return 0;
}

void ds45_detour_remove(ds45_detour_t *d)
{
	if (d->slot) {
		*d->slot = d->saved;
		d->slot  = NULL;
	}
}

/* ------------------------------------------------------------------------ */
/* Descriptor detour - the one that reaches other modules                    */
/*                                                                           */
/* Rewriting our own stub slot only redirects calls this module makes. Every  */
/* importer of a given function points at the same two word descriptor in the */
/* exporting library, so overwriting *that* redirects the whole process - the */
/* game included. This is what makes hooking a game's pad reads possible at   */
/* all from an injected module.                                              */
/* ------------------------------------------------------------------------ */

int ds45_detour_opd(void *target, void *replacement, ds45_opd_detour_t *d)
{
	uint32_t *opd, *rep;

	if (!target || !replacement)
		return -1;

	/* On PowerPC64 ELFv1 a function pointer *is* the address of the two word
	 * descriptor {entry point, TOC}. Taking the address of an imported
	 * function therefore hands us the descriptor the loader resolved, with no
	 * table walking and no dependency on where the process is laid out. */
	opd = (uint32_t *)target;
	rep = (uint32_t *)replacement;

	if (!plausible((uint32_t)(uintptr_t)opd))
		return -1;

	d->opd      = opd;
	d->saved[0] = opd[0];
	d->saved[1] = opd[1];

	/* The two words have to land together. A caller that reads the descriptor
	 * between two separate stores gets our entry point paired with libpad's
	 * TOC, and then runs our code against the wrong globals - a crash in the
	 * game process rather than merely a wrong stick value.
	 *
	 * Pointers are 4 bytes on this target (verified: sizeof(void *) == 4), so
	 * the pair is exactly 8 bytes and one naturally aligned store covers both.
	 * If either descriptor is not 8 byte aligned, fall back to the two stores
	 * rather than refuse to hook at all. */
	if ((((uintptr_t)opd | (uintptr_t)rep) & 7) == 0) {
		*(volatile uint64_t *)opd = *(const uint64_t *)rep;
	} else {
		opd[0] = rep[0];
		opd[1] = rep[1];
	}
	__asm__ volatile ("sync" ::: "memory");

	return 0;
}

void ds45_detour_opd_remove(ds45_opd_detour_t *d)
{
	if (d->opd) {
		/* Restored as one store too, for the same reason. */
		if (((uintptr_t)d->opd & 7) == 0) {
			uint64_t both = ((uint64_t)d->saved[0] << 32) | d->saved[1];

			*(volatile uint64_t *)d->opd = both;
		} else {
			d->opd[0] = d->saved[0];
			d->opd[1] = d->saved[1];
		}
		__asm__ volatile ("sync" ::: "memory");
		d->opd = NULL;
	}
}
