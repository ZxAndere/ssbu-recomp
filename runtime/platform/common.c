#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>

static FILE *log_file;
int sp_init(const char *path) {
    if (log_file) return SP_INVALID;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (path) {
        log_file = fopen(path, "ab");
        if (!log_file) return SP_IO;
    }
    return SP_OK;
}
void sp_shutdown(void) {
    if (log_file) fclose(log_file);
    log_file = NULL;
}
void sp_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    if (log_file) {
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fputc('\n', log_file);
        fflush(log_file);
    }
}
int sp_file_read(const char *path, uint64_t offset, void *data, size_t capacity,
                 size_t *bytes_read) {
    FILE *file;
    int result = SP_OK;
    if (bytes_read) *bytes_read = 0;
    if (!path || (!data && capacity) || !bytes_read || offset > INT64_MAX)
        return SP_INVALID;
    file = fopen(path, "rb");
    if (!file) return SP_IO;
#ifdef _WIN32
    if (_fseeki64(file, (int64_t)offset, SEEK_SET)) result = SP_IO;
#else
    if (fseeko(file, (off_t)offset, SEEK_SET)) result = SP_IO;
#endif
    if (!result && capacity) {
        *bytes_read = fread(data, 1, capacity, file);
        if (ferror(file)) result = SP_IO;
    }
    if (fclose(file)) result = SP_IO;
    return result;
}
