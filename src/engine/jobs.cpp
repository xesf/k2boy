// jobs.cpp — see jobs.h.

#include "engine/jobs.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "platform/platform.h"

namespace k2 {
namespace jobs {

namespace {

std::mutex              g_lock;
std::condition_variable g_wake, g_done;
int                     g_workers = 0;

// The batch being run: workers take indices until `next` passes `count`.
range_fn            g_fn = 0;
void*               g_user = 0;
size_t              g_count = 0;
std::atomic<size_t> g_next(0);
int                 g_busy = 0;         // workers still inside the batch
unsigned            g_batch = 0;        // bumped per batch so a worker runs each once

void run_items() {
    for (;;) {
        const size_t i = g_next.fetch_add(1);
        if (i >= g_count) return;
        g_fn(i, g_user);
    }
}

void worker(void*) {
    unsigned seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> l(g_lock);
            g_wake.wait(l, [&] { return g_batch != seen; });
            seen = g_batch;
        }
        run_items();
        {
            std::lock_guard<std::mutex> l(g_lock);
            if (--g_busy == 0) g_done.notify_one();
        }
    }
}

} // namespace

void init(int workers) {
    if (g_workers) return;
    const int spare = plat::cpu_count() - 1;
    if (workers > spare) workers = spare;
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
    {
        std::lock_guard<std::mutex> l(g_lock);
        g_fn = fn;
        g_user = user;
        g_count = count;
        g_next.store(0);
        g_busy = g_workers;
        ++g_batch;
    }
    g_wake.notify_all();
    run_items();                        // the calling thread works too
    std::unique_lock<std::mutex> l(g_lock);
    g_done.wait(l, [] { return g_busy == 0; });
}

} // namespace jobs
} // namespace k2
