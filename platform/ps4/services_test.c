#include "platform.h"
#include <stdio.h>
#include <string.h>
typedef struct { SpEvent *ready, *go; SpTls *tls; int failed; } SyncWork;
static void signal_worker(void *argument) {
    SyncWork *work = argument;
    if (sp_tls_get(work->tls) || sp_tls_set(work->tls, work) || sp_tls_get(work->tls) != work)
        work->failed = 1;
    if (sp_event_set(work->ready) || sp_event_wait(work->go, 2000000)) work->failed = 1;
}
int sp_services_test(const char *fixture, const char *scratch) {
    unsigned checks = 0, failures = 0;
#define VERIFY(expr) do { ++checks; if (!(expr)) { ++failures; sp_log("SERVICES FAIL %d: %s", __LINE__, #expr); } } while (0)
    SpMemory memory = {0};
    VERIFY(sp_memory_map(&memory, 64, NULL) == SP_OK);
    if (memory.data) {
        memset(memory.data, 42, 64);
        VERIFY(sp_memory_protect(&memory, SP_READ) == SP_OK);
        VERIFY(((unsigned char *)memory.data)[0] == 42);
        VERIFY(sp_memory_protect(&memory, SP_READ | SP_WRITE) == SP_OK);
        VERIFY(sp_memory_protect(&memory, SP_WRITE) == SP_INVALID);
        VERIFY(sp_memory_unmap(&memory) == SP_OK);
    }
    SpEvent *event = NULL;
    VERIFY(sp_event_create(&event, 0, 0) == SP_OK);
    if (event) {
        VERIFY(sp_event_wait(event, 10000) == SP_TIMEOUT);
        VERIFY(sp_event_set(event) == SP_OK);
        VERIFY(sp_event_wait(event, 0) == SP_OK);
        VERIFY(sp_event_wait(event, 0) == SP_TIMEOUT);
        VERIFY(sp_event_destroy(&event) == SP_OK);
    }
    SpSemaphore *sem = NULL;
    VERIFY(sp_semaphore_create(&sem, 0, 2) == SP_OK);
    if (sem) {
        VERIFY(sp_semaphore_wait(sem, 1000) == SP_TIMEOUT);
        VERIFY(sp_semaphore_post(sem, 2) == SP_OK);
        VERIFY(sp_semaphore_post(sem, 1) == SP_INVALID);
        VERIFY(sp_semaphore_wait(sem, 0) == SP_OK);
        VERIFY(sp_semaphore_wait(sem, 0) == SP_OK);
        VERIFY(sp_semaphore_wait(sem, 0) == SP_TIMEOUT);
        VERIFY(sp_semaphore_destroy(&sem) == SP_OK);
    }
    SyncWork work = {0};
    SpThread *thread = NULL;
    int setup = sp_event_create(&work.ready, 1, 0) || sp_event_create(&work.go, 0, 0) || sp_tls_create(&work.tls);
    VERIFY(!setup);
    if (!setup) {
        VERIFY(sp_tls_set(work.tls, &checks) == SP_OK);
        int started = sp_thread_start(&thread, signal_worker, &work);
        VERIFY(!started);
        if (!started) {
            VERIFY(sp_event_wait(work.ready, 2000000) == SP_OK);
            VERIFY(sp_event_wait(work.ready, 0) == SP_OK);
            VERIFY(sp_event_reset(work.ready) == SP_OK);
            VERIFY(sp_event_wait(work.ready, 0) == SP_TIMEOUT);
            VERIFY(sp_event_set(work.go) == SP_OK);
            if (sp_thread_join(&thread)) { sp_log("FATAL service thread join"); return 1; }
            VERIFY(!work.failed && sp_tls_get(work.tls) == &checks);
        }
    }
    if (work.ready) VERIFY(sp_event_destroy(&work.ready) == SP_OK);
    if (work.go) VERIFY(sp_event_destroy(&work.go) == SP_OK);
    if (work.tls) VERIFY(sp_tls_destroy(&work.tls) == SP_OK);
    SpFile *file = NULL;
    VERIFY(sp_file_open(&file, fixture, 0, 0) == SP_OK);
    if (file) {
        char data[5] = {0}; size_t done = 0; uint64_t size = 0;
        VERIFY(sp_file_size(file, &size) == SP_OK && size >= 19);
        VERIFY(sp_file_read_at(file, 5, data, 3, &done) == SP_OK && done == 3 && !memcmp(data, "PS4", 3));
        VERIFY(sp_file_write_at(file, 0, "X", 1, &done) == SP_INVALID);
        VERIFY(sp_file_close(&file) == SP_OK && !file);
    }
    if (scratch) {
        VERIFY(sp_file_open(&file, scratch, 1, 1) == SP_OK);
        if (file) {
            size_t done = 0; char data[4] = {0}; uint64_t size;
            VERIFY(sp_file_write_at(file, 0x100001ULL, "PS4", 3, &done) == SP_OK && done == 3);
            VERIFY(sp_file_flush(file) == SP_OK);
            VERIFY(sp_file_size(file, &size) == SP_OK && size == 0x100004ULL);
            VERIFY(sp_file_read_at(file, 0x100001ULL, data, 3, &done) == SP_OK && !memcmp(data, "PS4", 3));
            VERIFY(sp_file_read_at(file, 0x100000001ULL, data, 3, &done) == SP_OK && !done);
            VERIFY(sp_file_close(&file) == SP_OK);
            VERIFY(sp_file_open(&file, scratch, 1, 1) == SP_IO && !file);
        }
    }
#ifdef _WIN32
    SpModule *module = NULL; void *symbol = NULL;
    VERIFY(sp_module_open(&module, "kernel32.dll") == SP_OK);
    if (module) {
        VERIFY(sp_module_symbol(module, "GetTickCount64", &symbol) == SP_OK && symbol);
        VERIFY(sp_module_symbol(module, "ssbu_missing_symbol", &symbol) != SP_OK && !symbol);
        VERIFY(sp_module_close(&module) == SP_OK && !module);
    }
#endif
    sp_log("SERVICES RESULT: %u checks, %u failures", checks, failures);
    return failures ? 1 : 0;
#undef VERIFY
}
