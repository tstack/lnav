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

#ifndef __concise_index_hh
#define __concise_index_hh

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <lnav_log.hh>

class concise_index {

public:
    concise_index()
        : ci_bitmap(NULL), ci_map_size(0), ci_map_max_size(0),
          ci_literal_size(0), ci_size(0) {
        this->ensure_size(1);
    };

    ~concise_index() {
        free(this->ci_bitmap);
        this->ci_bitmap = NULL;
    };

    uint64_t size(void) const {
        return this->ci_size;
    };

    bool empty(void) const {
        return this->ci_size == 0;
    };

    void clear(void) {
        memset(this->ci_bitmap, 0, sizeof(uint64_t) * this->ci_map_size);
        this->ci_literal_size = 0;
        this->ci_size = 0;
        this->ci_map_size = 1;
    };

    bool push_back(bool v) {
        uint64_t &lit_or_rle_word = this->get_last_word();

        if (this->is_rle(lit_or_rle_word)) {
            if (this->get_rle_value(lit_or_rle_word) == v &&
                this->have_run_length_available(lit_or_rle_word)) {
                this->inc_run_length(lit_or_rle_word);
                this->ci_size += 1;

                ensure(this->ci_literal_size == 0);
                return true;
            }
            if (!this->ensure_size(this->ci_map_size + 1)) {
                return false;
            }
        }

        if (this->ci_literal_size == LITERAL_SIZE) {
            this->ensure_size(this->ci_map_size + 1);
            this->ci_literal_size = 0;
        }

        uint64_t &lit_word = this->get_last_word();

        if (v) {
            lit_word |= this->bit_in_word(this->ci_literal_size);
        }
        this->ci_literal_size += 1;
        this->ci_size += 1;
        this->compact_last_word();

        ensure(this->ci_literal_size <= LITERAL_SIZE);

        return true;
    };

    bool push_back(uint64_t v, uint64_t len = BITS_PER_WORD) {
        uint64_t &lit_or_rle_word = this->get_last_word();

        require(len <= BITS_PER_WORD);

        if (len == 0) {
            return true;
        }

        for (int lpc = len; lpc < BITS_PER_WORD; lpc++) {
            uint64_t mask = 1ULL << (len - 1);
            uint64_t bit_to_set_or_clear = (1ULL << lpc);

            if (v & mask) {
                v |= bit_to_set_or_clear;
            } else {
                v &= ~bit_to_set_or_clear;
            }
        }

        if ((v == 0ULL || v == ~0ULL) &&
            (this->is_rle(lit_or_rle_word) || this->ci_literal_size == 0)) {
            bool bv = v;

            if (this->is_rle(lit_or_rle_word)) {
                if (this->get_rle_value(lit_or_rle_word) == bv &&
                    this->have_run_length_available(lit_or_rle_word, len)) {
                    this->inc_run_length(lit_or_rle_word);
                    this->ci_size += len;
                    return true;
                }
                if (!this->ensure_size(this->ci_map_size + 1)) {
                    return false;
                }
            }

            uint64_t &last_word = this->get_last_word();

            last_word = (
                RLE_MODE |
                (v & VAL_MASK) |
                len);
            this->ci_size += len;
            this->compact_last_word();
            return true;
        }

        int words_needed = 0;

        if (this->is_rle(lit_or_rle_word)) {
            words_needed = 1;
            if (len > LITERAL_SIZE) {
                words_needed += 1;
            }
            if (!this->ensure_size(this->ci_map_size + words_needed)) {
                return false;
            }
        }
        else {
            if ((this->ci_literal_size + len) > LITERAL_SIZE) {
                words_needed = 1;
            }
            if (!this->ensure_size(this->ci_map_size + words_needed)) {
                return false;
            }
            words_needed += 1;
        }

        uint64_t &prev_word = this->ci_bitmap[this->ci_map_size - words_needed];
        uint64_t &last_word = this->get_last_word();

        prev_word |= (v << this->ci_literal_size) & LITERAL_MASK;
        if (words_needed == 2) {
            last_word = v >> this->ci_literal_size;
        }

        this->ci_literal_size = len - this->ci_literal_size;
        this->ci_size += len;

        this->compact_last_word();

        return true;
    };

    struct const_iterator {
        const_iterator(const concise_index *ci = NULL, uint64_t map_index = 0,
            uint64_t bit_index = 0)
            : i_parent(ci), i_map_index(map_index), i_bit_index(bit_index) {
        };

        void increment(uint64_t amount) {
            uint64_t &curr_word = this->i_parent->ci_bitmap[this->i_map_index];

            if (this->i_parent->is_rle(curr_word)) {
                if ((this->i_bit_index + amount) <
                    this->i_parent->run_length(curr_word)) {
                    this->i_bit_index += amount;
                }
                else {
                    amount -= (this->i_parent->run_length(curr_word) -
                        this->i_bit_index);
                    this->i_map_index += 1;
                    this->i_bit_index = amount;
                }
            } else {
                if ((this->i_bit_index + amount) < LITERAL_SIZE) {
                    this->i_bit_index += amount;
                }
                else {
                    amount -= LITERAL_SIZE - this->i_bit_index;
                    this->i_map_index += 1;
                    this->i_bit_index = amount;
                }
            }

            if (this->i_map_index >= (this->i_parent->ci_map_size - 1)) {
                const uint64_t &last_word = this->i_parent->get_last_word();

                if (this->i_map_index >= this->i_parent->ci_map_size) {
                    this->i_map_index = this->i_parent->ci_map_size - 1;
                    this->i_bit_index = ~0ULL;
                }
                if (this->i_parent->is_rle(last_word)) {
                    if (this->i_bit_index >
                        this->i_parent->run_length(last_word)) {
                        this->i_bit_index = this->i_parent->run_length(last_word);
                    }
                }
                else {
                    if (this->i_bit_index > this->i_parent->ci_literal_size) {
                        this->i_bit_index = this->i_parent->ci_literal_size;
                    }
                }
            }

            ensure(this->i_map_index < this->i_parent->ci_map_size);
        };

        void next_word() {
            this->increment(BITS_PER_WORD);
        };

        const_iterator &operator++(void) {
            this->increment(1);
            return *this;
        };

        bool operator!=(const const_iterator &rhs) const {
            return (this->i_map_index != rhs.i_map_index ||
                this->i_bit_index != rhs.i_bit_index);
        };

        bool operator==(const const_iterator &rhs) const {
            return (this->i_map_index == rhs.i_map_index ||
                this->i_bit_index == rhs.i_bit_index);
        };

        bool operator*(void) const {
            uint64_t &word = this->i_parent->ci_bitmap[this->i_map_index];

            if (this->i_parent->is_rle(word)) {
                return this->i_parent->get_rle_value(word);
            }

            return word & this->i_parent->bit_in_word(this->i_bit_index);
        };

        uint64_t get_word(size_t &valid_bits_out) const {
            uint64_t &word = this->i_parent->ci_bitmap[this->i_map_index];
            uint64_t bits_remaining = BITS_PER_WORD;
            uint64_t next_index = this->i_map_index + 1;
            uint64_t retval = 0;

            valid_bits_out = 0;

            if (this->i_parent->is_literal(word)) {
                retval = (word >> this->i_bit_index);
                if (this->i_map_index == (this->i_parent->ci_map_size - 1)) {
                    valid_bits_out = this->i_parent->ci_literal_size;
                } else {
                    valid_bits_out = (LITERAL_SIZE - this->i_bit_index);
                }
                bits_remaining -= valid_bits_out;
            } else {
                uint64_t min_len = std::min(
                    this->i_parent->run_length(word) - this->i_bit_index,
                    bits_remaining);
                bits_remaining -= min_len;
                valid_bits_out += min_len;
                if (this->i_parent->get_rle_value(word)) {
                    retval = ~0ULL;
                } else {
                    retval = 0ULL;
                }
                retval = retval >> bits_remaining;
            }

            if (bits_remaining && next_index < this->i_parent->ci_map_size) {
                uint64_t &next_word = this->i_parent->ci_bitmap[next_index];
                uint64_t upper_bits = 0;

                if (this->i_parent->is_literal(next_word)) {
                    upper_bits = next_word;
                    if (this->i_map_index == (this->i_parent->ci_map_size - 1)) {
                        valid_bits_out += this->i_parent->ci_literal_size;
                        valid_bits_out = std::min(
                            (size_t)BITS_PER_WORD, valid_bits_out);
                    }
                } else if (this->i_parent->get_rle_value(next_word)) {
                    upper_bits = ~0ULL;
                    valid_bits_out = BITS_PER_WORD;
                } else {
                    upper_bits = 0;
                    valid_bits_out = BITS_PER_WORD;
                }
                retval |= (upper_bits << (BITS_PER_WORD - bits_remaining));
            }

            return retval;
        };

        const concise_index *i_parent;
        uint64_t i_map_index;
        uint64_t i_bit_index;
    };

    const_iterator begin() {
        return const_iterator(this);
    };

    const_iterator end() {
        uint64_t &word = this->get_last_word();

        if (this->is_rle(word)) {
            return const_iterator(this, this->ci_map_size - 1, this->run_length(word));
        } else {
            return const_iterator(this, this->ci_map_size - 1,
                this->ci_literal_size);
        }
    };

// private:
    static const uint64_t MODE_MASK = 0x8000000000000000ULL;
    static const uint64_t VAL_MASK  = 0x4000000000000000ULL;
    static const uint64_t POS_MASK  = 0x3f00000000000000ULL;
    static const uint64_t LEN_MASK  = 0x00ffffffffffffffULL;
    static const uint64_t LITERAL_MASK = ~MODE_MASK;

    static const uint64_t RLE_MODE = MODE_MASK;
    static const uint64_t LIT_MODE = 0ULL;

    static const uint64_t BITS_PER_WORD = sizeof(uint64_t) * 8;
    static const uint64_t LITERAL_SIZE = BITS_PER_WORD - 1;
    static const uint64_t BITMAP_INCREMENT = 64;

    bool is_literal(uint64_t v) const {
        return (v & MODE_MASK) == LIT_MODE;
    };

    bool is_rle(uint64_t v) const {
        return (v & MODE_MASK) == RLE_MODE;
    };

    bool get_rle_value(uint64_t v) const {
        return (v & VAL_MASK);
    };

    int pos_index(uint64_t v) const {
        return (v & POS_MASK) >> (64 - 6 - 1);
    };

    uint64_t run_length(uint64_t v) const {
        return (v & LEN_MASK);
    };

    void inc_run_length(uint64_t &v, uint64_t len = 1) const {
        v += len;
    };

    bool have_run_length_available(uint64_t v, uint64_t len = 1) const {
        return (this->run_length(v) + len) < LEN_MASK;
    };

    uint64_t bitmap_size_for_bits(uint64_t bits_size) {
        return (bits_size + LITERAL_SIZE - 1) / LITERAL_SIZE;
    };

    uint64_t &get_word(uint64_t bit_index) {
        uint64_t word_index = bit_index / sizeof(uint64_t);

        return this->ci_bitmap[word_index];
    };

    uint64_t &get_last_word() {
        return this->ci_bitmap[this->ci_map_size - 1];
    };

    const uint64_t &get_last_word() const {
        return this->ci_bitmap[this->ci_map_size - 1];
    };

    uint64_t bit_in_word(uint64_t bit_index) const {
        uint64_t bit_in_word_index = bit_index % LITERAL_SIZE;

        return (1ULL << bit_in_word_index);
    };

    void compact_last_word() {
        if (this->ci_size == 0 || this->ci_literal_size % LITERAL_SIZE) {
            return;
        }

        uint64_t &last_word = this->get_last_word();

        if (last_word != 0 && last_word != LITERAL_MASK) {
            return;
        }

        last_word = (
            RLE_MODE |
            (last_word & VAL_MASK) |
            LITERAL_SIZE);

        this->ci_literal_size = 0;
    };

    bool ensure_size(uint64_t map_size) {
        if (map_size <= this->ci_map_max_size) {
            this->ci_map_size = map_size;
            return true;
        }

        uint64_t new_map_size = map_size + BITMAP_INCREMENT;
        uint64_t *new_bitmap = (uint64_t *)realloc(
            this->ci_bitmap, new_map_size * sizeof(uint64_t));

        if (new_bitmap == NULL) {
            return false;
        }

        memset(&new_bitmap[this->ci_map_size], 0,
            (new_map_size - this->ci_map_size) * sizeof(uint64_t));
        this->ci_bitmap = new_bitmap;
        this->ci_map_size = map_size;
        this->ci_map_max_size = new_map_size;

        return true;
    };

    uint64_t *ci_bitmap;
    uint64_t ci_map_size;
    uint64_t ci_map_max_size;
    uint64_t ci_literal_size;
    uint64_t ci_size;
};

#endif
