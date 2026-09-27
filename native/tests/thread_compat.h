#ifndef PVR_TEST_THREAD_COMPAT_H
#define PVR_TEST_THREAD_COMPAT_H
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef HANDLE pthread_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
static int pthread_mutex_init(pthread_mutex_t *m, void *a) { (void)a; InitializeSRWLock(m); return 0; }
static int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
static int pthread_mutex_lock(pthread_mutex_t *m) { AcquireSRWLockExclusive(m); return 0; }
static int pthread_mutex_unlock(pthread_mutex_t *m) { ReleaseSRWLockExclusive(m); return 0; }
static int pthread_cond_init(pthread_cond_t *c, void *a) { (void)a; InitializeConditionVariable(c); return 0; }
static int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
static int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    return SleepConditionVariableSRW(c, m, INFINITE, 0) ? 0 : (int)GetLastError();
}
static int pthread_cond_broadcast(pthread_cond_t *c) { WakeAllConditionVariable(c); return 0; }
typedef struct { void *(*entry)(void *); void *arg; } TestThreadStart;
static unsigned __stdcall test_thread_start(void *p) {
    TestThreadStart start = *(TestThreadStart *)p; free(p); start.entry(start.arg); return 0;
}
static int pthread_create(pthread_t *thread, void *attr, void *(*entry)(void *), void *arg) {
    (void)attr;
    TestThreadStart *s = malloc(sizeof(*s)); if (!s) return 12;
    s->entry = entry; s->arg = arg;
    *thread = (HANDLE)_beginthreadex(NULL, 0, test_thread_start, s, 0, NULL);
    if (!*thread) { free(s); return 11; }
    return 0;
}
static int pthread_join(pthread_t thread, void **result) {
    (void)result;
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) return 22;
    CloseHandle(thread); return 0;
}
static int nanosleep(const struct timespec *duration, struct timespec *remaining) {
    (void)remaining; Sleep((DWORD)(duration->tv_sec * 1000 + (duration->tv_nsec + 999999) / 1000000)); return 0;
}
static unsigned __stdcall test_deadline(void *seconds) {
    Sleep((DWORD)(uintptr_t)seconds * 1000); _Exit(124);
}
static unsigned alarm(unsigned seconds) {
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, test_deadline, (void *)(uintptr_t)seconds, 0, NULL);
    if (h) CloseHandle(h);
    return 0;
}
#endif
#endif
