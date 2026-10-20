#ifndef SP_GUEST_H
#define SP_GUEST_H
#include "platform.h"
#include "runtime.h"
typedef struct SpGuestImage SpGuestImage;
typedef int (*SpGuestResolver)(const char *name, uint64_t *guest_address, void *user);
typedef struct {
    const FnEntry *functions; size_t function_count;
    const DynSym *symbols; size_t symbol_count;
    uint64_t image_fingerprint;
} SpGuestProgram;
/* Parse and map AArch64 ELF segments into host allocations. Never execute the
 * ARM64 bytes. All sizes/offsets are checked; image limit is 256 MiB. */
int sp_guest_load(SpGuestImage **out, const char *path);
int sp_guest_load_at(SpGuestImage **out, const char *path, uint64_t base);
int sp_guest_export(SpGuestImage *image, const char *name, uint64_t *address, unsigned *kind);
int sp_guest_close(SpGuestImage **image);
void *sp_guest_pointer(SpGuestImage *image, uint64_t address, size_t size, int write);
int sp_guest_relocate(SpGuestImage *image, SpGuestResolver resolve, void *user, size_t *unresolved);
uint64_t sp_guest_fingerprint(const SpGuestImage *image);
uint64_t sp_guest_entry(const SpGuestImage *image);
size_t sp_guest_size(const SpGuestImage *image);
int sp_guest_initializers(SpGuestImage *image, uint64_t *address, size_t *count);
/* Enter translated C only after fingerprint + sorted function table checks.
 * This bootstrap currently permits one active context on its calling thread. */
int sp_guest_run(SpGuestImage *image, const SpGuestProgram *program, uint64_t entry, Ctx *context);
const char *sp_guest_last_error(void);
void *sp_recomp_access(uint64_t address, size_t size, int write);
int sp_recomp_test(const char *path);
int sp_game_bootstrap(const char *path);
int sp_guest_batch(const char *path);
int sp_guest_inspect(const char *path);
#endif
