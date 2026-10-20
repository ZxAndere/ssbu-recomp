#ifndef SP_LOADER_LOOKUP_H
#define SP_LOADER_LOOKUP_H
#include "../guest.h"
/* Internal resolver query. Undefined ELF weak symbols stay null unless a
 * loaded module actually defines them; symbolic fallback thunks do not. */
int sp_guest_weak_undefined(SpGuestImage *image,const char *name,int *weak);
#endif
