#include "platform.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
int sp_services_test(const char *fixture, const char *scratch);
#define CHECK(test) do { ++checks; if (!(test)) { ++failures; \
    sp_log("FAIL line %d: %s", __LINE__, #test); } } while (0)

/* These are host-runtime hooks, not game functions. Needed by stx64 below. */
void stx_diag(Ctx *context, uint64_t address, uint64_t expected, uint64_t seen,
              uint64_t value, int bits) {
    (void)context; (void)address; (void)expected; (void)seen; (void)value; (void)bits;
}
typedef struct { SpMutex *mutex; unsigned *counter; int error; } Worker;
static void worker(void *argument) {
    Worker *work = argument;
    for (unsigned i = 0; i < 10000; ++i) {
        if (sp_mutex_lock(work->mutex)) { work->error = 1; return; }
        ++*work->counter;
        if (sp_mutex_unlock(work->mutex)) { work->error = 1; return; }
    }
}
static void memory_checks(void) {
    SpMemory memory = {0}, other = {0};
    size_t page = sp_page_size();
    CHECK(page && !(page & (page - 1)));
    CHECK(sp_memory_map(NULL, 1, NULL) == SP_INVALID);
    CHECK(sp_memory_map(&memory, 0, NULL) == SP_INVALID);
    CHECK(sp_memory_map(&memory, SIZE_MAX, NULL) == SP_INVALID);
    int result = sp_memory_map(&memory, page + 17, NULL);
    CHECK(result == SP_OK);
    if (result) return;
    CHECK(memory.size == page * 2);
    CHECK(!((uintptr_t)memory.data % page));
    unsigned char *bytes = memory.data;
    CHECK(bytes[0] == 0 && bytes[memory.size - 1] == 0);
    memset(bytes, 0xa5, memory.size);
    CHECK(bytes[0] == 0xa5 && bytes[memory.size - 1] == 0xa5);
    CHECK(sp_memory_map(&memory, page, NULL) == SP_INVALID);
    /* An occupied address must never be replaced by the second mapping. */
    CHECK(sp_memory_map(&other, page, memory.data) != SP_OK);
    CHECK(!other.data && !other.size && bytes[0] == 0xa5);
    CHECK(sp_memory_unmap(&memory) == SP_OK);
    CHECK(!memory.data && !memory.size);
    CHECK(sp_memory_unmap(&memory) == SP_INVALID);
}
static void thread_checks(void) {
    SpMutex *mutex = NULL;
    SpThread *threads[2] = {0};
    Worker work[2];
    unsigned counter = 0;
    CHECK(sp_thread_start(NULL, worker, NULL) == SP_INVALID);
    CHECK(sp_thread_start(&threads[0], NULL, NULL) == SP_INVALID);
    CHECK(sp_thread_join(&threads[0]) == SP_INVALID);
    CHECK(sp_mutex_lock(NULL) == SP_INVALID);
    CHECK(sp_mutex_unlock(NULL) == SP_INVALID);
    int result = sp_mutex_create(&mutex);
    CHECK(result == SP_OK);
    if (result) return;
    CHECK(sp_mutex_create(&mutex) == SP_INVALID);
    for (unsigned i = 0; i < 2; ++i) {
        work[i] = (Worker){mutex, &counter, 0};
        CHECK(sp_thread_start(&threads[i], worker, &work[i]) == SP_OK);
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (threads[i]) {
            result = sp_thread_join(&threads[i]);
            CHECK(result == SP_OK);
            /* Retain live objects if a join fails. The diagnostic then stops. */
            if (result) { sp_log("FATAL: worker join failed"); exit(2); }
        }
        CHECK(!threads[i] && !work[i].error);
    }
    CHECK(counter == 20000);
    CHECK(sp_mutex_destroy(&mutex) == SP_OK);
    CHECK(!mutex);
    CHECK(sp_mutex_destroy(&mutex) == SP_INVALID);
}
static void file_checks(const char *path) {
    char bytes[64] = {0};
    size_t count = 777;
    CHECK(sp_file_read(NULL, 0, bytes, sizeof bytes, &count) == SP_INVALID && !count);
    CHECK(sp_file_read(path, UINT64_MAX, bytes, sizeof bytes, &count) == SP_INVALID);
    CHECK(sp_file_read(path, 0, NULL, 1, &count) == SP_INVALID);
    CHECK(sp_file_read(path, 0, bytes, sizeof bytes, NULL) == SP_INVALID);
    int result = sp_file_read(path, 0, bytes, sizeof bytes, &count);
    CHECK(result == SP_OK);
    CHECK(count >= 19 && !memcmp(bytes, "SSBU-PS4-DIAGNOSTIC", 19));
    CHECK(sp_file_read(path, 5, bytes, 3, &count) == SP_OK && count == 3);
    CHECK(!memcmp(bytes, "PS4", 3));
    CHECK(sp_file_read(path, 0x100000000ULL, bytes, sizeof bytes, &count) == SP_OK && !count);
    CHECK(sp_file_read(path, 0, NULL, 0, &count) == SP_OK && !count);
    CHECK(sp_file_read("/this-path-does-not-exist/fixture", 0, bytes, 1, &count) == SP_IO);
}
static void runtime_checks(void) {
    /* Synthetic values only: exercise the actual portable lifter support header. */
    Ctx context = {0};
    uint64_t value = 42, address = (uint64_t)(uintptr_t)&value;
    CHECK(sizeof(V128) == 16 && sizeof(context.x[0]) == 8);
    CHECK(adds64(&context, UINT64_MAX, 1, 0) == 0);
    CHECK(context.Z == 1 && context.C == 1 && context.V == 0);
    CHECK(adds32(&context, 0x7fffffff, 1, 0) == 0x80000000);
    CHECK(context.N == 1 && context.V == 1 && context.C == 0);
    wr64(address, 64);
    CHECK(rd64(address) == 64);
    context.ex_addr = address; context.ex_val = 64;
    CHECK(stx64(&context, address, 77) == 0 && value == 77);
    context.ex_addr = address; context.ex_val = 64;
    CHECK(stx64(&context, address, 99) == 1 && value == 77);
}
int sp_diagnostic(const char *fixture) {
    sp_log("SSBU PS4 platform diagnostic; %s; no game content", sp_platform_name());
    memory_checks(); thread_checks(); file_checks(fixture); runtime_checks();
    uint64_t before = sp_now_us();
    sp_sleep_ms(20);
    uint64_t after = sp_now_us();
    CHECK(after >= before && after - before >= 10000);
    sp_log("RESULT: %u checks, %u failures", checks, failures);
    return failures ? 1 : 0;
}
#ifndef SSBU_PS4
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) { fprintf(stderr, "usage: diagnostic <fixture> [new scratch file]\n"); return 2; }
    if (sp_init(NULL)) return 2;
    int result = sp_diagnostic(argv[1]);
    result |= sp_services_test(argv[1], argc > 2 ? argv[2] : NULL);
    sp_shutdown();
    return result;
}
#endif
