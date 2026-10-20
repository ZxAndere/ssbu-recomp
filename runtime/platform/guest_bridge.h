/* Force-include only when compiling generated ARM64 -> C sources for this
 * target. Host-side code keeps its native pointers. */
#ifndef SP_GUEST_BRIDGE_H
#define SP_GUEST_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
void *sp_recomp_access(uint64_t address, size_t size, int write);
extern uint64_t sp_recomp_budget;
void sp_recomp_checkpoint(uint64_t pc);
#define SP_GUEST_POLL(pc) do { if (!sp_recomp_budget--) sp_recomp_checkpoint(pc); } while (0)
#define RECOMP_ACCESS(a,n,w) sp_recomp_access((a),(n),(w))
#endif
