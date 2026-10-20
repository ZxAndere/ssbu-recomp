/* Small platform boundary for the standalone diagnostic. Not yet the game host. */
#ifndef SSBU_PLATFORM_H
#define SSBU_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

enum { SP_OK = 0, SP_INVALID = -1, SP_SYSTEM = -2, SP_IO = -3,
       SP_ADDRESS = -4 };
typedef struct SpThread SpThread;
typedef struct SpMutex SpMutex;
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
/* Binary read at a 64-bit offset; EOF is success with fewer bytes. No writes. */
int sp_file_read(const char *path, uint64_t offset, void *data, size_t capacity,
                 size_t *bytes_read);
#endif
