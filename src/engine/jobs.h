// jobs.h — a small worker pool for splitting per-frame work across cores.
//
// The engine is single-threaded by design (the original was); this only
// fans out loops whose iterations touch nothing but their own data - posing
// each actor's skeleton, for one. Workers are platform threads
// (plat::thread_start), pinned to their own cores where the platform can:
// on the Vita the main thread runs on core 0 and the workers on 1 and 2.
//
// C++11.

#ifndef K2_ENGINE_JOBS_H
#define K2_ENGINE_JOBS_H

#include <stddef.h>

namespace k2 {
namespace jobs {

// Starts up to `workers` threads (fewer if the platform has fewer spare
// cores). Zero keeps everything on the calling thread.
void init(int workers);
int  worker_count();

// Calls fn(i, user) for every i in [0, count), spread over the workers and
// the calling thread, and returns when all are done. Iterations must be
// independent. Not reentrant: call from the main thread only.
typedef void (*range_fn)(size_t i, void* user);
void parallel_for(size_t count, range_fn fn, void* user);

} // namespace jobs
} // namespace k2

#endif // K2_ENGINE_JOBS_H
