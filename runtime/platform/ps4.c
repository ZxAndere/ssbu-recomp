#include <orbis/libkernel.h>
#include <stdlib.h>
#include "platform.h"

/* Use scePthread entry points, not musl pthread_* wrappers with a different ABI. */
struct SpThread { OrbisPthread native; SpThreadFn fn; void *argument; };
struct SpMutex { OrbisPthreadMutex native; };
const char *sp_platform_name(void) { return "PS4 OpenOrbis x86-64 (experimental)"; }
size_t sp_page_size(void) { return 0x4000; }
uint64_t sp_now_us(void) { return sceKernelGetProcessTime(); }
void sp_sleep_ms(unsigned milliseconds) {
    while (milliseconds > 1000) { sceKernelUsleep(1000000); milliseconds -= 1000; }
    sceKernelUsleep(milliseconds * 1000);
}
int sp_memory_map(SpMemory *out, size_t size, void *address) {
    const size_t page = 0x4000;
    size_t rounded;
    void *pointer = NULL;
    int result;
    if (!out || out->data || out->size || !size || size > SIZE_MAX - (page - 1))
        return SP_INVALID;
    rounded = (size + page - 1) / page * page;
    /* MAP_FIXED is deliberately absent: never replace an existing mapping. */
    /* SDK sys/mman.h carries Linux MAP_ANON=0x20. sceKernelMmap uses the
     * FreeBSD/Orbis ABI: READ|WRITE=3, PRIVATE=2, ANON=0x1000. */
    result = sceKernelMmap(address, rounded, 3, 2 | 0x1000, -1, 0, &pointer);
    if (result) return SP_SYSTEM;
    if (address && pointer != address) {
        sceKernelMunmap(pointer, rounded);
        return SP_ADDRESS;
    }
    out->data = pointer; out->size = rounded;
    return SP_OK;
}
int sp_memory_unmap(SpMemory *memory) {
    if (!memory || !memory->data || !memory->size) return SP_INVALID;
    if (sceKernelMunmap(memory->data, memory->size)) return SP_SYSTEM;
    memory->data = NULL; memory->size = 0;
    return SP_OK;
}
static void *thread_entry(void *argument) {
    SpThread *thread = argument;
    thread->fn(thread->argument);
    return NULL;
}
int sp_thread_start(SpThread **out, SpThreadFn fn, void *argument) {
    SpThread *thread;
    if (!out || *out || !fn) return SP_INVALID;
    thread = calloc(1, sizeof *thread);
    if (!thread) return SP_SYSTEM;
    thread->fn = fn; thread->argument = argument;
    if (scePthreadCreate(&thread->native, NULL, thread_entry, thread, "ssbu-diag")) {
        free(thread); return SP_SYSTEM;
    }
    *out = thread; return SP_OK;
}
int sp_thread_join(SpThread **thread) {
    if (!thread || !*thread) return SP_INVALID;
    if (scePthreadJoin((*thread)->native, NULL)) return SP_SYSTEM;
    free(*thread); *thread = NULL; return SP_OK;
}
int sp_mutex_create(SpMutex **out) {
    SpMutex *mutex;
    if (!out || *out) return SP_INVALID;
    mutex = calloc(1, sizeof *mutex);
    if (!mutex) return SP_SYSTEM;
    if (scePthreadMutexInit(&mutex->native, NULL, "ssbu-diag")) {
        free(mutex); return SP_SYSTEM;
    }
    *out = mutex; return SP_OK;
}
int sp_mutex_lock(SpMutex *mutex) {
    if (!mutex) return SP_INVALID;
    return scePthreadMutexLock(&mutex->native) ? SP_SYSTEM : SP_OK;
}
int sp_mutex_unlock(SpMutex *mutex) {
    if (!mutex) return SP_INVALID;
    return scePthreadMutexUnlock(&mutex->native) ? SP_SYSTEM : SP_OK;
}
int sp_mutex_destroy(SpMutex **mutex) {
    if (!mutex || !*mutex) return SP_INVALID;
    if (scePthreadMutexDestroy(&(*mutex)->native)) return SP_SYSTEM;
    free(*mutex); *mutex = NULL; return SP_OK;
}
