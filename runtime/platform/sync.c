#include "platform.h"
#include <stdlib.h>

struct SpEvent { SpMutex *mutex; SpCond *cond; int manual, signaled; };
struct SpSemaphore { SpMutex *mutex; SpCond *cond; unsigned count, maximum; };
static uint64_t remaining(uint64_t start, uint64_t timeout) {
    uint64_t elapsed = sp_now_us() - start;
    return elapsed >= timeout ? 0 : timeout - elapsed;
}
int sp_event_create(SpEvent **out, int manual, int signaled) {
    if (!out || *out) return SP_INVALID;
    SpEvent *event = calloc(1, sizeof *event);
    if (!event) return SP_SYSTEM;
    if (sp_mutex_create(&event->mutex) || sp_cond_create(&event->cond)) {
        if (event->mutex) sp_mutex_destroy(&event->mutex);
        free(event); return SP_SYSTEM;
    }
    event->manual = !!manual; event->signaled = !!signaled;
    *out = event; return SP_OK;
}
int sp_event_set(SpEvent *event) {
    if (!event) return SP_INVALID;
    int rc = sp_mutex_lock(event->mutex);
    if (rc) return rc;
    event->signaled = 1;
    rc = sp_cond_signal(event->cond, event->manual);
    int unlock = sp_mutex_unlock(event->mutex);
    return rc ? rc : unlock;
}
int sp_event_reset(SpEvent *event) {
    if (!event) return SP_INVALID;
    int rc = sp_mutex_lock(event->mutex);
    if (rc) return rc;
    event->signaled = 0;
    return sp_mutex_unlock(event->mutex);
}
int sp_event_wait(SpEvent *event, uint64_t timeout) {
    if (!event) return SP_INVALID;
    uint64_t start = sp_now_us();
    int rc = sp_mutex_lock(event->mutex);
    if (rc) return rc;
    while (!event->signaled) {
        uint64_t left = timeout == UINT64_MAX ? UINT64_MAX : remaining(start, timeout);
        if (!left) { rc = SP_TIMEOUT; break; }
        rc = sp_cond_wait(event->cond, event->mutex, left);
        if (rc && rc != SP_TIMEOUT) break;
        if (rc == SP_TIMEOUT && !event->signaled) break;
    }
    if (event->signaled) { rc = SP_OK; if (!event->manual) event->signaled = 0; }
    int unlock = sp_mutex_unlock(event->mutex);
    return rc ? rc : unlock;
}
int sp_event_destroy(SpEvent **event) {
    if (!event || !*event) return SP_INVALID;
    /* No live waiters are allowed during destruction. */
    if (sp_cond_destroy(&(*event)->cond) || sp_mutex_destroy(&(*event)->mutex)) return SP_SYSTEM;
    free(*event); *event = NULL; return SP_OK;
}
int sp_semaphore_create(SpSemaphore **out, unsigned initial, unsigned maximum) {
    if (!out || *out || !maximum || initial > maximum) return SP_INVALID;
    SpSemaphore *sem = calloc(1, sizeof *sem);
    if (!sem) return SP_SYSTEM;
    if (sp_mutex_create(&sem->mutex) || sp_cond_create(&sem->cond)) {
        if (sem->mutex) sp_mutex_destroy(&sem->mutex);
        free(sem); return SP_SYSTEM;
    }
    sem->count = initial; sem->maximum = maximum;
    *out = sem; return SP_OK;
}
int sp_semaphore_post(SpSemaphore *sem, unsigned count) {
    if (!sem || !count) return SP_INVALID;
    int rc = sp_mutex_lock(sem->mutex);
    if (rc) return rc;
    if (count > sem->maximum - sem->count) rc = SP_INVALID;
    else { sem->count += count; rc = sp_cond_signal(sem->cond, count > 1); }
    int unlock = sp_mutex_unlock(sem->mutex);
    return rc ? rc : unlock;
}
int sp_semaphore_wait(SpSemaphore *sem, uint64_t timeout) {
    if (!sem) return SP_INVALID;
    uint64_t start = sp_now_us();
    int rc = sp_mutex_lock(sem->mutex);
    if (rc) return rc;
    while (!sem->count) {
        uint64_t left = timeout == UINT64_MAX ? UINT64_MAX : remaining(start, timeout);
        if (!left) { rc = SP_TIMEOUT; break; }
        rc = sp_cond_wait(sem->cond, sem->mutex, left);
        if (rc && rc != SP_TIMEOUT) break;
        if (rc == SP_TIMEOUT && !sem->count) break;
    }
    if (sem->count) { --sem->count; rc = SP_OK; }
    int unlock = sp_mutex_unlock(sem->mutex);
    return rc ? rc : unlock;
}
int sp_semaphore_destroy(SpSemaphore **sem) {
    if (!sem || !*sem) return SP_INVALID;
    if (sp_cond_destroy(&(*sem)->cond) || sp_mutex_destroy(&(*sem)->mutex)) return SP_SYSTEM;
    free(*sem); *sem = NULL; return SP_OK;
}
