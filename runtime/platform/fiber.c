#include "platform.h"
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
typedef struct {
    uint64_t rsp,rbx,rbp,r12,r13,r14,r15;
    uint32_t mxcsr; uint16_t control,padding;
} Context;
_Static_assert(sizeof(Context)==64,"SysV context layout");
extern void sp_context_swap(Context *from,const Context *to);
#endif
typedef struct FiberThread FiberThread;
struct SpFiber {
    FiberThread *owner; struct SpFiber *caller;
    SpThreadFn fn; void *argument,*local[16];
    int finished; unsigned references;
#ifdef _WIN32
    void *native;
#else
    Context context; SpMemory allocation;
#endif
};
struct FiberThread { SpFiber root,*current; unsigned live; int converted; };
static SpTls *thread_key;
static int initialized;
static FiberThread *thread_state(void) {
    int expected=0;
    if (__atomic_compare_exchange_n(&initialized,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
        __atomic_store_n(&initialized,sp_tls_create(&thread_key) ? -1 : 2,__ATOMIC_RELEASE);
    while (__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)==1) sp_sleep_ms(1);
    if (__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=2) return NULL;
    FiberThread *state=sp_tls_get(thread_key);
    if (state) return state;
    state=calloc(1,sizeof *state);
    if (!state) return NULL;
    state->root.owner=state; state->current=&state->root;
#ifdef _WIN32
    state->converted=!IsThreadAFiber();
    state->root.native=state->converted ? ConvertThreadToFiberEx(NULL,FIBER_FLAG_FLOAT_SWITCH) : GetCurrentFiber();
    if (!state->root.native) { free(state); return NULL; }
#endif
    if (sp_tls_set(thread_key,state)) {
#ifdef _WIN32
        if (state->converted) ConvertFiberToThread();
#endif
        free(state); return NULL;
    }
    return state;
}
static void transfer(SpFiber *from,SpFiber *to) {
    from->owner->current=to;
#ifdef _WIN32
    SwitchToFiber(to->native);
#else
    sp_context_swap(&from->context,&to->context);
#endif
}
#ifdef _WIN32
static VOID WINAPI fiber_entry(void *argument) {
    SpFiber *fiber=argument;
#else
static void fiber_entry(void) {
    FiberThread *state=thread_state();
    SpFiber *fiber=state->current;
#endif
    fiber->fn(fiber->argument);
    fiber->finished=1;
    transfer(fiber,fiber->caller);
    abort();
}
int sp_fiber_create(SpFiber **out,size_t stack_size,SpThreadFn fn,void *argument) {
    if (!out || *out || !fn || stack_size<0x4000 || stack_size>32*1024*1024) return SP_INVALID;
    FiberThread *owner=thread_state();
    if (!owner) return SP_SYSTEM;
    SpFiber *fiber=calloc(1,sizeof *fiber);
    if (!fiber) return SP_SYSTEM;
    fiber->owner=owner; fiber->fn=fn; fiber->argument=argument;
#ifdef _WIN32
    fiber->native=CreateFiberEx(stack_size,stack_size,FIBER_FLAG_FLOAT_SWITCH,fiber_entry,fiber);
    if (!fiber->native) { free(fiber); return SP_SYSTEM; }
#else
    size_t page=sp_page_size(); stack_size=(stack_size+page-1)/page*page;
    if (sp_memory_map(&fiber->allocation,stack_size+page*2,NULL)) { free(fiber); return SP_SYSTEM; }
    SpMemory low={fiber->allocation.data,page},high={(char *)fiber->allocation.data+page+stack_size,page};
    if (sp_memory_protect(&low,0) || sp_memory_protect(&high,0)) {
        sp_memory_unmap(&fiber->allocation); free(fiber); return SP_SYSTEM;
    }
    uintptr_t top=(uintptr_t)fiber->allocation.data+page+stack_size;
    /* ret enters a C function with RSP%16==8, as required by SysV. */
    uint64_t *stack=(uint64_t *)(top-16);
    stack[0]=(uint64_t)(uintptr_t)fiber_entry;
    fiber->context.rsp=(uint64_t)(uintptr_t)stack;
    fiber->context.mxcsr=0x1f80; fiber->context.control=0x037f;
#endif
    ++owner->live; *out=fiber; return SP_OK;
}
int sp_fiber_resume(SpFiber *fiber) {
    FiberThread *state=thread_state();
    if (!fiber || !state || fiber->owner!=state || fiber==state->current || fiber->finished) return SP_INVALID;
    SpFiber *caller=state->current;
    fiber->caller=caller; ++caller->references;
    transfer(caller,fiber);
    --caller->references; return SP_OK;
}
int sp_fiber_yield(void) {
    FiberThread *state=thread_state();
    if (!state || state->current==&state->root || !state->current->caller) return SP_INVALID;
    SpFiber *current=state->current;
    transfer(current,current->caller); return SP_OK;
}
int sp_fiber_finished(SpFiber *fiber) { return fiber && fiber->owner==thread_state() ? fiber->finished : SP_INVALID; }
int sp_fiber_destroy(SpFiber **out) {
    FiberThread *state=thread_state();
    if (!out || !*out || !state || (*out)->owner!=state || (*out)==state->current || (*out)->references) return SP_INVALID;
    SpFiber *fiber=*out;
#ifdef _WIN32
    DeleteFiber(fiber->native);
#else
    if (sp_memory_unmap(&fiber->allocation)) return SP_SYSTEM;
#endif
    --state->live; free(fiber); *out=NULL; return SP_OK;
}
void *sp_fiber_get_local(unsigned slot) {
    FiberThread *state=thread_state(); return state && slot<16 ? state->current->local[slot] : NULL;
}
int sp_fiber_set_local(unsigned slot,void *value) {
    FiberThread *state=thread_state(); if (!state || slot>=16) return SP_INVALID;
    state->current->local[slot]=value; return SP_OK;
}
int sp_fiber_thread_shutdown(void) {
    FiberThread *state=thread_state();
    if (!state || state->current!=&state->root || state->live) return SP_INVALID;
#ifdef _WIN32
    if (state->converted && !ConvertFiberToThread()) return SP_SYSTEM;
#endif
    sp_tls_set(thread_key,NULL); free(state); return SP_OK;
}
