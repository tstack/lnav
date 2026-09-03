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

#include "sqlitepp.hh"

#include "base/lnav_log.hh"
#include "sqlite-extension-func.hh"

namespace sqlitepp {

const char* ERROR_PREFIX = "lnav-error:";

}

namespace lnav::sql {

sqlite3*
thread_local_db()
{
    struct holder {
        auto_sqlite3 h_db;
        bool h_tried{false};
    };

    thread_local holder tl_holder;

    if (tl_holder.h_tried) {
        return tl_holder.h_db.in();
    }
    tl_holder.h_tried = true;

    // Separate connections on separate threads need at least multi-thread
    // mode; 0 means SQLite was built with threading compiled out.
    if (sqlite3_threadsafe() == 0) {
        log_warning("sqlite3 is not thread-safe, no thread-local DB");
        return nullptr;
    }

    if (sqlite3_open(":memory:", tl_holder.h_db.out()) != SQLITE_OK) {
        log_error("unable to open a thread-local DB -- %s",
                  sqlite3_errmsg(tl_holder.h_db.in()));
        tl_holder.h_db.reset();
        return nullptr;
    }

    register_sqlite_funcs(tl_holder.h_db.in(),
                          sqlite_thread_safe_registration_funcs);
    register_collation_functions(tl_holder.h_db.in());

    return tl_holder.h_db.in();
}

}  // namespace lnav::sql
