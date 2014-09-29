/**
* Copyright (c) 2014, Timothy Stack
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
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* @file chunky_index.hh
*/

#ifndef __chunky_index_hh
#define __chunky_index_hh

#include <stdlib.h>

#include <list>
#include <vector>

#include "lnav_log.hh"

template<typename T, size_t CHUNK_SIZE = 4096>
class chunky_index {

public:
    chunky_index() : ci_generation(0), ci_merge_chunk(NULL), ci_size(0) {
    };

    ~chunky_index() {
        this->clear();
    };

    size_t size() const {
        return this->ci_size;
    };

    size_t chunk_count() const {
        return this->ci_completed_chunks.size();
    };

    T& operator[](size_t index) {
        size_t chunk_index = index / CHUNK_SIZE;
        assert(chunk_index < this->chunk_count());
        struct chunk *target_chunk = this->ci_completed_chunks[chunk_index];
        return target_chunk->c_body[index % CHUNK_SIZE];
    };

    void clear() {
        while (!this->ci_completed_chunks.empty()) {
            delete this->ci_completed_chunks.back();
            this->ci_completed_chunks.pop_back();
        }
        while (!this->ci_pending_chunks.empty()) {
            delete this->ci_pending_chunks.front();
            this->ci_pending_chunks.pop_front();
        }
        if (this->ci_merge_chunk != NULL) {
            delete this->ci_merge_chunk;
            this->ci_merge_chunk = NULL;
        }
        this->ci_size = 0;
    };

    void reset() {
        for (size_t lpc = 0; lpc < this->ci_completed_chunks.size(); lpc++) {
            this->ci_pending_chunks.push_back(this->ci_completed_chunks[lpc]);
        }
        this->ci_completed_chunks.clear();
        this->ci_generation += 1;
    };

    template<typename Comparator>
    void merge_value(const T &val, Comparator comparator) {
        this->merge_up_to(&val, comparator);
        this->ci_merge_chunk->push_back(val);

        this->ci_size += 1;
    };

    void merge_value(const T &val) {
        this->merge_value(val, less_comparator());
    };

    void finish() {
        this->merge_up_to(NULL, null_comparator());
        if (this->ci_merge_chunk != NULL) {
            if (this->ci_merge_chunk->empty()) {
                delete this->ci_merge_chunk;
                this->ci_merge_chunk = NULL;
            }
            else {
                this->ci_completed_chunks.push_back(this->ci_merge_chunk);
                this->ci_merge_chunk = NULL;
            }
        }
    };

private:
    void skip_chunks(const T *val) {
        while (!this->ci_pending_chunks.empty() && this->ci_pending_chunks.front()->skippable(val)) {
            struct chunk *skipped_chunk = this->ci_pending_chunks.front();
            this->ci_pending_chunks.pop_front();
            skipped_chunk->c_consumed = 0;
            skipped_chunk->c_generation = this->ci_generation;
            this->ci_completed_chunks.push_back(skipped_chunk);
        }
    };

    struct null_comparator {
        int operator()(const T &val, const T &other) const {
            return 0;
        };
    };

    struct less_comparator {
        int operator()(const T &val, const T &other) const {
            if (val < other) {
                return -1;
            }
            else if (other < val) {
                return 1;
            }
            else {
                return 0;
            }
        };
    };

    template<typename Comparator>
    void merge_up_to(const T *val, Comparator comparator) {
        this->skip_chunks(val);

        do {
            if (this->ci_merge_chunk != NULL && this->ci_merge_chunk->full()) {
                this->ci_completed_chunks.push_back(this->ci_merge_chunk);
                this->ci_merge_chunk = NULL;
            }
            if (this->ci_merge_chunk == NULL) {
                this->ci_merge_chunk = new chunk(this->ci_generation);
            }

            if (!this->ci_pending_chunks.empty()) {
                struct chunk *next_chunk = this->ci_pending_chunks.front();
                while (((val == NULL) || (comparator(next_chunk->front(), *val) < 0)) &&
                        !this->ci_merge_chunk->full()) {
                    this->ci_merge_chunk->push_back(next_chunk->consume());
                    if (next_chunk->empty()) {
                        this->ci_pending_chunks.pop_front();
                        delete next_chunk;
                        if (!this->ci_pending_chunks.empty()) {
                            next_chunk = this->ci_pending_chunks.front();
                        } else {
                            break;
                        }
                    }
                }
            }
        } while (this->ci_merge_chunk->full());
    };

    struct chunk {
        chunk(unsigned long gen) : c_generation(gen), c_consumed(0), c_used(0) { };

        bool empty() const {
            return this->c_consumed == this->c_used;
        };

        bool full() const {
            return this->c_used == CHUNK_SIZE;
        };

        bool skippable(const T *val) const {
            return this->c_consumed == 0 && this->full() && (
                    val == NULL || (this->back() <= *val));
        };

        const T &front() const {
            return this->c_body[this->c_consumed];
        };

        const T &consume() {
            this->c_consumed += 1;
            return this->c_body[this->c_consumed - 1];
        };

        const T &back() const {
            return this->c_body[this->c_used - 1];
        };

        void push_back(const T &val) {
            this->c_body[this->c_used] = val;
            this->c_used += 1;
        };

        unsigned long c_generation;
        T c_body[CHUNK_SIZE];
        size_t c_consumed;
        size_t c_used;
    };

    unsigned long ci_generation;
    std::vector<struct chunk *> ci_completed_chunks;
    struct chunk *ci_merge_chunk;
    std::list<struct chunk *> ci_pending_chunks;

    size_t ci_size;
};

#endif
