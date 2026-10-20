#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include "platform.h"

struct SpThread { HANDLE handle; SpThreadFn fn; void *argument; };
struct SpMutex { CRITICAL_SECTION native; };
const char *sp_platform_name(void) { return "Windows x86-64"; }
size_t sp_page_size(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}
uint64_t sp_now_us(void) {
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000 +
           (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000 /
           (uint64_t)frequency.QuadPart;
}
void sp_sleep_ms(unsigned milliseconds) { Sleep(milliseconds); }
int sp_memory_map(SpMemory *out, size_t size, void *address) {
    size_t page = sp_page_size(), rounded;
    void *pointer;
    if (!out || out->data || out->size || !size || size > SIZE_MAX - (page - 1))
        return SP_INVALID;
    rounded = (size + page - 1) / page * page;
    pointer = VirtualAlloc(address, rounded, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!pointer) return SP_SYSTEM;
    if (address && pointer != address) {
        VirtualFree(pointer, 0, MEM_RELEASE);
        return SP_ADDRESS;
    }
    out->data = pointer; out->size = rounded;
    return SP_OK;
}
int sp_memory_unmap(SpMemory *memory) {
    if (!memory || !memory->data || !memory->size) return SP_INVALID;
    if (!VirtualFree(memory->data, 0, MEM_RELEASE)) return SP_SYSTEM;
    memory->data = NULL; memory->size = 0;
    return SP_OK;
}
static DWORD WINAPI thread_entry(void *argument) {
    SpThread *thread = argument;
    thread->fn(thread->argument);
    return 0;
}
int sp_thread_start(SpThread **out, SpThreadFn fn, void *argument) {
    SpThread *thread;
    if (!out || *out || !fn) return SP_INVALID;
    thread = calloc(1, sizeof *thread);
    if (!thread) return SP_SYSTEM;
    thread->fn = fn; thread->argument = argument;
    thread->handle = CreateThread(NULL, 0, thread_entry, thread, 0, NULL);
    if (!thread->handle) { free(thread); return SP_SYSTEM; }
    *out = thread;
    return SP_OK;
}
int sp_thread_join(SpThread **thread) {
    if (!thread || !*thread) return SP_INVALID;
    if (WaitForSingleObject((*thread)->handle, INFINITE) != WAIT_OBJECT_0)
        return SP_SYSTEM;
    if (!CloseHandle((*thread)->handle)) return SP_SYSTEM;
    free(*thread); *thread = NULL;
    return SP_OK;
}
int sp_mutex_create(SpMutex **out) {
    SpMutex *mutex;
    if (!out || *out) return SP_INVALID;
    mutex = calloc(1, sizeof *mutex);
    if (!mutex) return SP_SYSTEM;
    if (!InitializeCriticalSectionEx(&mutex->native, 0, 0)) {
        free(mutex); return SP_SYSTEM;
    }
    *out = mutex;
    return SP_OK;
}
int sp_mutex_lock(SpMutex *mutex) {
    if (!mutex) return SP_INVALID;
    EnterCriticalSection(&mutex->native); return SP_OK;
}
int sp_mutex_unlock(SpMutex *mutex) {
    if (!mutex) return SP_INVALID;
    LeaveCriticalSection(&mutex->native); return SP_OK;
}
int sp_mutex_destroy(SpMutex **mutex) {
    if (!mutex || !*mutex) return SP_INVALID;
    DeleteCriticalSection(&(*mutex)->native);
    free(*mutex); *mutex = NULL;
    return SP_OK;
}
