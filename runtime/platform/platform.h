/* Small platform boundary for the standalone diagnostic. Not yet the game host. */
#ifndef SSBU_PLATFORM_H
#define SSBU_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

enum { SP_OK = 0, SP_INVALID = -1, SP_SYSTEM = -2, SP_IO = -3,
       SP_ADDRESS = -4, SP_TIMEOUT = -5, SP_UNSUPPORTED = -6 };
typedef struct SpThread SpThread;
typedef struct SpMutex SpMutex;
typedef struct SpCond SpCond;
typedef struct SpEvent SpEvent;
typedef struct SpSemaphore SpSemaphore;
typedef struct SpFile SpFile;
typedef struct SpModule SpModule;
typedef struct SpTls SpTls;
typedef struct SpFiber SpFiber;
typedef void (*SpThreadFn)(void *);
typedef struct { void *data; size_t size; } SpMemory;

/* Main-thread logging; log files are optional, opened in append mode. */
int sp_init(const char *log_path);
void sp_shutdown(void);
void sp_log(const char *format, ...);
const char *sp_platform_name(void);
size_t sp_page_size(void);
uint64_t sp_now_us(void);
void sp_sleep_ms(unsigned milliseconds);
/* Read/write only. An address is a non-destructive hint: fail if not exact.
 * Output structs/handles must be empty. Release only once, after all users stop. */
int sp_memory_map(SpMemory *out, size_t size, void *address);
int sp_memory_unmap(SpMemory *memory);
int sp_thread_start(SpThread **out, SpThreadFn fn, void *argument);
int sp_thread_join(SpThread **thread);
int sp_mutex_create(SpMutex **out);
int sp_mutex_lock(SpMutex *mutex);
int sp_mutex_unlock(SpMutex *mutex);
int sp_mutex_destroy(SpMutex **mutex);
/* Cond wait atomically releases/reacquires mutex. UINT64_MAX = indefinite.
 * Predicates must be checked in a loop by the caller. */
int sp_cond_create(SpCond **out);
int sp_cond_wait(SpCond *cond, SpMutex *mutex, uint64_t timeout_us);
int sp_cond_signal(SpCond *cond, int broadcast);
int sp_cond_destroy(SpCond **cond);
int sp_event_create(SpEvent **out, int manual_reset, int signaled);
int sp_event_set(SpEvent *event);
int sp_event_reset(SpEvent *event);
int sp_event_wait(SpEvent *event, uint64_t timeout_us);
int sp_event_destroy(SpEvent **event);
int sp_semaphore_create(SpSemaphore **out, unsigned initial, unsigned maximum);
int sp_semaphore_post(SpSemaphore *semaphore, unsigned count);
int sp_semaphore_wait(SpSemaphore *semaphore, uint64_t timeout_us);
int sp_semaphore_destroy(SpSemaphore **semaphore);
int sp_tls_create(SpTls **out);
int sp_tls_set(SpTls *key, void *value);
void *sp_tls_get(SpTls *key);
int sp_tls_destroy(SpTls **key);
/* Module lifetime must exceed every use of resolved symbols. Orbis PRX only
 * on PS4; Windows DLLs are not accepted as PS4 modules. No downloads occur. */
int sp_module_open(SpModule **out, const char *path);
int sp_module_symbol(SpModule *module, const char *name, void **symbol);
int sp_module_close(SpModule **module);
enum { SP_READ = 1, SP_WRITE = 2 };
int sp_memory_protect(SpMemory *memory, unsigned access);
/* Serialized random-access handle, read-only by default. Writable opens never
 * truncate; exclusive_create refuses an existing file. */
int sp_file_open(SpFile **out, const char *path, int writable, int exclusive_create);
int sp_file_size(SpFile *file, uint64_t *size);
int sp_file_read_at(SpFile *file, uint64_t offset, void *data, size_t size, size_t *done);
int sp_file_write_at(SpFile *file, uint64_t offset, const void *data, size_t size, size_t *done);
int sp_file_flush(SpFile *file);
int sp_file_close(SpFile **file);
/* Cooperative fibers are pinned to their creating host thread. Destroy only
 * a suspended/finished fiber. No stack migration or C++ stack unwinding. */
int sp_fiber_create(SpFiber **out, size_t stack_size, SpThreadFn fn, void *argument);
int sp_fiber_resume(SpFiber *fiber);
int sp_fiber_yield(void);
int sp_fiber_finished(SpFiber *fiber);
int sp_fiber_destroy(SpFiber **fiber);
void *sp_fiber_get_local(unsigned slot);
int sp_fiber_set_local(unsigned slot, void *value);
int sp_fiber_thread_shutdown(void);
/* Binary read at a 64-bit offset; EOF is success with fewer bytes. No writes. */
int sp_file_read(const char *path, uint64_t offset, void *data, size_t capacity,
                 size_t *bytes_read);
#endif
