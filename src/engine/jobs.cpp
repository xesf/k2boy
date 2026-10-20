// jobs.cpp — see jobs.h.
//
// Synchronised with platform semaphores only: one the workers wait on for
// a batch, one the caller waits on until each worker has finished it.
// Indices are handed out by an atomic counter.

#include "engine/jobs.h"

#include <atomic>

#include "platform/platform.h"

namespace k2 {
namespace jobs {

namespace {

int        g_workers = 0;
plat::sema g_wake = 0;          // signalled once per worker per batch
plat::sema g_done = 0;          // signalled by each worker when its share is done

range_fn            g_fn = 0;
void*               g_user = 0;
size_t              g_count = 0;
std::atomic<size_t> g_next(0);

void run_items() {
    for (;;) {
        const size_t i = g_next.fetch_add(1);
        if (i >= g_count) return;
        g_fn(i, g_user);
    }
}

void worker(void*) {
    for (;;) {
        plat::sema_wait(g_wake);
        run_items();
        plat::sema_signal(g_done, 1);
    }
}

} // namespace

void init(int workers) {
    if (g_workers) return;
    const int spare = plat::cpu_count() - 1;
    if (workers > spare) workers = spare;
    if (workers <= 0) return;
    g_wake = plat::sema_create(0);
    g_done = plat::sema_create(0);
    if (!g_wake || !g_done) return;
    for (int i = 0; i < workers; ++i)
        if (plat::thread_start(worker, 0, 1 + i, "k2 worker")) ++g_workers;
}

int worker_count() { return g_workers; }

void parallel_for(size_t count, range_fn fn, void* user) {
    if (count == 0) return;
    if (g_workers == 0 || count == 1) {
        for (size_t i = 0; i < count; ++i) fn(i, user);
        return;
    }
    // The semaphore signal publishes these to the workers.
    g_fn = fn;
    g_user = user;
    g_count = count;
    g_next.store(0);
    plat::sema_signal(g_wake, g_workers);
    run_items();                        // the calling thread works too
    for (int i = 0; i < g_workers; ++i) plat::sema_wait(g_done);
}

} // namespace jobs
} // namespace k2
