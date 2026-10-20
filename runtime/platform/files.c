#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

struct SpFile { FILE *native; SpMutex *mutex; int writable; };
static int seek_file(FILE *file, uint64_t offset) {
    if (offset > INT64_MAX) return SP_INVALID;
#ifdef _WIN32
    return _fseeki64(file, (int64_t)offset, SEEK_SET) ? SP_IO : SP_OK;
#else
    return fseeko(file, (off_t)offset, SEEK_SET) ? SP_IO : SP_OK;
#endif
}
int sp_file_open(SpFile **out, const char *path, int writable, int exclusive_create) {
    if (!out || *out || !path || (exclusive_create && !writable)) return SP_INVALID;
    SpFile *file = calloc(1, sizeof *file);
    if (!file) return SP_SYSTEM;
    file->native = fopen(path, exclusive_create ? "w+bx" : writable ? "r+b" : "rb");
    if (!file->native) { free(file); return SP_IO; }
    if (sp_mutex_create(&file->mutex)) { fclose(file->native); free(file); return SP_SYSTEM; }
    file->writable = writable;
    *out = file; return SP_OK;
}
int sp_file_size(SpFile *file, uint64_t *size) {
    if (!file || !size) return SP_INVALID;
    if (sp_mutex_lock(file->mutex)) return SP_SYSTEM;
#ifdef _WIN32
    int rc = _fseeki64(file->native, 0, SEEK_END) ? SP_IO : SP_OK;
    int64_t value = _ftelli64(file->native);
#else
    int rc = fseeko(file->native, 0, SEEK_END) ? SP_IO : SP_OK;
    int64_t value = ftello(file->native);
#endif
    if (value < 0) rc = SP_IO;
    if (!rc) *size = (uint64_t)value;
    sp_mutex_unlock(file->mutex); return rc;
}
static int transfer(SpFile *file, uint64_t offset, void *data, size_t size, size_t *done, int write) {
    if (done) *done = 0;
    if (!file || !done || (!data && size) || (write && !file->writable) ||
        offset > INT64_MAX || size > (uint64_t)INT64_MAX - offset) return SP_INVALID;
    if (sp_mutex_lock(file->mutex)) return SP_SYSTEM;
    clearerr(file->native);
    int rc = seek_file(file->native, offset);
    if (!rc && size) {
        *done = write ? fwrite(data, 1, size, file->native) : fread(data, 1, size, file->native);
        if (ferror(file->native) || (write && *done != size)) rc = SP_IO;
    }
    sp_mutex_unlock(file->mutex); return rc;
}
int sp_file_read_at(SpFile *file, uint64_t offset, void *data, size_t size, size_t *done) {
    return transfer(file, offset, data, size, done, 0);
}
int sp_file_write_at(SpFile *file, uint64_t offset, const void *data, size_t size, size_t *done) {
    return transfer(file, offset, (void *)data, size, done, 1);
}
int sp_file_flush(SpFile *file) {
    if (!file) return SP_INVALID;
    if (sp_mutex_lock(file->mutex)) return SP_SYSTEM;
    int rc = fflush(file->native) ? SP_IO : SP_OK;
    sp_mutex_unlock(file->mutex); return rc;
}
int sp_file_close(SpFile **file) {
    if (!file || !*file) return SP_INVALID;
    int rc = fclose((*file)->native) ? SP_IO : SP_OK;
    sp_mutex_destroy(&(*file)->mutex);
    free(*file); *file = NULL; return rc;
}
