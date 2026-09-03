/**
 * Copyright (c) 2026, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file parallel_for.hh
 */

#ifndef lnav_parallel_for_hh
#define lnav_parallel_for_hh

#include <chrono>
#include <cstddef>
#include <functional>

namespace lnav {

/**
 * @return How many workers to fan out over `count` items when the caller has
 * no preference.  Capped well below the core count -- the work this exists
 * for is I/O-bound enough that more threads mostly add contention.
 */
size_t default_worker_count(size_t count);

/**
 * Run body(0), body(1), ... body(count - 1) across `width` threads and return
 * once every one of them has finished.
 *
 * The pool lives only for the duration of this call, which is deliberate.
 * Indexing shares a process with code that fork()s (grep_proc, pipers), and a
 * fork while a worker holds the intern-string table lock or the log mutex
 * hands the child an already-locked mutex and a deadlock on its first log
 * call.  Confining the threads to a barrier is what guarantees no fork can
 * overlap them.
 *
 * While the workers run, `tick` is called on the *calling* thread about every
 * `tick_interval` -- that is where a caller drives a progress bar.  It runs
 * concurrently with the workers, so it must not touch anything they write.
 *
 * A `width` of 0 or 1, or a `count` of 1, runs everything inline on the
 * calling thread and creates no threads at all -- but only when no `tick` was
 * given.  A caller that passes one is saying it has work to do while the body
 * runs, so it always gets a worker to wait on, even for a single item.
 *
 * An exception out of `body` is logged and that one item is abandoned; the
 * rest of the fan-out still completes.  `body` is called from several threads
 * at once and must be safe for that.
 */
void parallel_for_each(size_t count,
                       size_t width,
                       const std::function<void(size_t)>& body,
                       const std::function<void()>& tick = {},
                       std::chrono::milliseconds tick_interval
                       = std::chrono::milliseconds{30});

}  // namespace lnav

#endif
