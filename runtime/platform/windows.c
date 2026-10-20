#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include "platform.h"

struct SpThread { HANDLE handle; SpThreadFn fn; void *argument; };
struct SpMutex { CRITICAL_SECTION native; };
struct SpCond { CONDITION_VARIABLE native; };
struct SpModule { HMODULE native; };
struct SpTls { DWORD native; };
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
int sp_cond_create(SpCond **out) {
    if (!out || *out) return SP_INVALID;
    SpCond *cond = calloc(1, sizeof *cond);
    if (!cond) return SP_SYSTEM;
    InitializeConditionVariable(&cond->native); *out = cond; return SP_OK;
}
int sp_cond_wait(SpCond *cond, SpMutex *mutex, uint64_t timeout) {
    if (!cond || !mutex) return SP_INVALID;
    DWORD ms = timeout == UINT64_MAX ? INFINITE :
        (timeout / 1000 >= INFINITE - 1 ? INFINITE - 1 : (DWORD)(timeout / 1000 + !!(timeout % 1000)));
    if (SleepConditionVariableCS(&cond->native, &mutex->native, ms)) return SP_OK;
    return GetLastError() == ERROR_TIMEOUT ? SP_TIMEOUT : SP_SYSTEM;
}
int sp_cond_signal(SpCond *cond, int broadcast) {
    if (!cond) return SP_INVALID;
    if (broadcast) WakeAllConditionVariable(&cond->native); else WakeConditionVariable(&cond->native);
    return SP_OK;
}
int sp_cond_destroy(SpCond **cond) {
    if (!cond || !*cond) return SP_INVALID;
    free(*cond); *cond = NULL; return SP_OK;
}
int sp_tls_create(SpTls **out) {
    if (!out || *out) return SP_INVALID;
    SpTls *key = malloc(sizeof *key);
    if (!key) return SP_SYSTEM;
    key->native = TlsAlloc();
    if (key->native == TLS_OUT_OF_INDEXES) { free(key); return SP_SYSTEM; }
    *out = key; return SP_OK;
}
int sp_tls_set(SpTls *key, void *value) {
    if (!key) return SP_INVALID;
    return TlsSetValue(key->native, value) ? SP_OK : SP_SYSTEM;
}
void *sp_tls_get(SpTls *key) { return key ? TlsGetValue(key->native) : NULL; }
int sp_tls_destroy(SpTls **key) {
    if (!key || !*key) return SP_INVALID;
    if (!TlsFree((*key)->native)) return SP_SYSTEM;
    free(*key); *key = NULL; return SP_OK;
}
int sp_module_open(SpModule **out, const char *path) {
    if (!out || *out || !path) return SP_INVALID;
    SpModule *module = calloc(1, sizeof *module);
    if (!module) return SP_SYSTEM;
    module->native = LoadLibraryA(path);
    if (!module->native) { free(module); return SP_SYSTEM; }
    *out = module; return SP_OK;
}
int sp_module_symbol(SpModule *module, const char *name, void **symbol) {
    if (!module || !name || !symbol) return SP_INVALID;
    *symbol = (void *)(uintptr_t)GetProcAddress(module->native, name);
    return *symbol ? SP_OK : SP_SYSTEM;
}
int sp_module_close(SpModule **module) {
    if (!module || !*module) return SP_INVALID;
    if (!FreeLibrary((*module)->native)) return SP_SYSTEM;
    free(*module); *module = NULL; return SP_OK;
}
int sp_memory_protect(SpMemory *memory, unsigned access) {
    if (!memory || !memory->data || !memory->size || access & ~(SP_READ | SP_WRITE) ||
        access == SP_WRITE) return SP_INVALID;
    DWORD old, mode = access & SP_WRITE ? PAGE_READWRITE : access & SP_READ ? PAGE_READONLY : PAGE_NOACCESS;
    return VirtualProtect(memory->data, memory->size, mode, &old) ? SP_OK : SP_SYSTEM;
}
