#include "platform.h"
#include <orbis/libkernel.h>

int sp_diagnostic(const char *fixture);
int sp_services_test(const char *fixture, const char *scratch);
int main(void) {
    if (sp_init("/data/ssbu-ps4-diagnostic.log")) sp_init(NULL);
    int result = sp_diagnostic("/app0/fixture.txt");
    result |= sp_services_test("/app0/fixture.txt", NULL);
    /* These tiny non-destructive probes expose the first game-host blocker.
     * A failure does not prevent the rest of the standalone diagnostic. */
    const uint64_t addresses[] = {0x7100000000ULL, 0x7300000000ULL};
    for (unsigned i = 0; i < 2; ++i) {
        SpMemory memory = {0};
        int rc = sp_memory_map(&memory, sp_page_size(), (void *)(uintptr_t)addresses[i]);
        sp_log("GUEST ADDRESS PROBE 0x%llx: status %d (0 is exact)",
               (unsigned long long)addresses[i], rc);
        if (!rc) sp_memory_unmap(&memory);
    }
    sp_log("Diagnostic finished, result %d. Close the title from the PS menu.", result);
    sp_shutdown();
    /* Keep a completed title idle. Returning through this SDK's CRT is not yet
     * validated on hardware. No busy spin, process kill or console shutdown. */
    for (;;) sceKernelUsleep(1000000);
}
