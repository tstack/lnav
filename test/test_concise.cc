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
 */

#include "config.h"

#include <assert.h>
#include <time.h>
#include <sys/time.h>

#include "concise_index.hh"

static void push_bits(concise_index &ci, bool v, size_t count)
{
    for (size_t lpc = 0; lpc < count; lpc++) {
        ci.push_back(v);
    }
}

static void check_bits(concise_index::const_iterator &iter, bool v, size_t count)
{
    for (size_t lpc = 0; lpc < count; lpc++) {
        assert(v == *iter);
        ++iter;
    }
}

int main(int argc, char *argv[])
{
    concise_index::const_iterator iter;
    concise_index ci;
    size_t valid_bits;
    uint64_t v;

    assert(ci.empty());

    ci.push_back(true);

    assert(ci.size() == 1);
    assert(!ci.empty());

    iter = ci.begin();
    assert(iter != ci.end());
    v = iter.get_word(valid_bits);
    assert(v == 1ULL);
    assert(valid_bits == 1);
    assert(*iter == true);

    ++iter;
    assert(iter == ci.end());

    ci.clear();

    ci.push_back_word(0, 0);
    assert(ci.size() == 0);
    assert(ci.empty());
    v = iter.get_word(valid_bits);
    assert(v == 0);
    assert(valid_bits == 0);
    assert(ci.begin() == ci.end());


    push_bits(ci, true, concise_index::BITS_PER_WORD);
    assert(ci.size() == concise_index::BITS_PER_WORD);
    assert(!ci.empty());
    iter = ci.begin();
    check_bits(iter, true, concise_index::BITS_PER_WORD);
    assert(iter == ci.end());

    push_bits(ci, false, concise_index::BITS_PER_WORD);
    assert(ci.size() == concise_index::BITS_PER_WORD * 2);
    assert(!ci.empty());
    iter = ci.begin();
    iter.increment(concise_index::BITS_PER_WORD);
    check_bits(iter, false, concise_index::BITS_PER_WORD);
    assert(iter == ci.end());


    ci.clear();

    ci.push_back_word(~0ULL, concise_index::BITS_PER_WORD - 1);
    assert(ci.size() == concise_index::BITS_PER_WORD - 1);
    iter = ci.begin();
    v = iter.get_word(valid_bits);
    assert(v == (~0ULL >> 1));
    assert(valid_bits == concise_index::BITS_PER_WORD - 1);

    ci.push_back(true);
    assert(ci.size() == concise_index::BITS_PER_WORD);
    iter = ci.begin();
    v = iter.get_word(valid_bits);
    assert(v == ~0ULL);
    assert(valid_bits == concise_index::BITS_PER_WORD);


    ci.clear();
    ci.push_back_word(~0ULL);
    ci.push_back_word(0ULL);
    iter = ci.begin();
    v = iter.get_word(valid_bits);
    assert(v == ~0ULL);
    assert(valid_bits == concise_index::BITS_PER_WORD);
    iter.next_word();
    v = iter.get_word(valid_bits);
    assert(v == 0ULL);
    assert(valid_bits == concise_index::BITS_PER_WORD);
}
