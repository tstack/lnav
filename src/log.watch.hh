/**
 * Copyright (c) 2022, Timothy Stack
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
 */

#ifndef lnav_log_watch_hh
#define lnav_log_watch_hh

#include "logfile.hh"

namespace lnav::log::watch {

/**
 * Evaluate the watch expressions for a line and publish an event for each one
 * that matches.  Steps statements on, and writes to, the main database, so
 * only the thread that owns that connection may call this.
 */
void eval_with(logfile& lf, logfile::iterator ll);

/**
 * Evaluate the watch expressions for a line, from any thread.
 *
 * The expressions run against lnav::sql::thread_local_db(), so no connection
 * is shared.  A match is then handed to the main loop to publish, because
 * lnav_events lives on the main connection and a user trigger on it can run
 * anything -- which also means the event appears once the loop gets to it
 * rather than at the moment of the match.
 *
 * Hand-offs are batched, so a caller has to end a scan with flush_pending().
 */
void eval_for(logfile& lf, logfile::iterator ll);

/**
 * Send whatever eval_for() has batched on this thread.
 *
 * Called at the end of a file's scan.  Not required for correctness on its
 * own -- a batch for one file is also sent when the thread starts on the next
 * -- but without it the last file's matches would sit unpublished.
 */
void flush_pending();

/**
 * @return Whether any watch expression is enabled.
 *
 * An indexing pass has to know up front whether evaluation happens at all.
 */
bool any_enabled();

}

#endif
