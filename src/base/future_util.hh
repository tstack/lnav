/**
 * Copyright (c) 2020, Timothy Stack
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

#ifndef lnav_future_util_hh
#define lnav_future_util_hh

#include <future>

template<class T>
std::future<std::decay_t<T>> make_ready_future( T&& t ) {
    std::promise<std::decay_t<T>> pr;
    auto r = pr.get_future();
    pr.set_value(std::forward<T>(t));
    return r;
}

template<typename T>
class future_queue {
public:
    future_queue(std::function<void(const T&)> processor)
        : fq_processor(processor) {};

    ~future_queue() {
        this->pop_to();
    }

    void push_back(std::future<T>&& f) {
        this->fq_deque.emplace_back(std::move(f));
        this->pop_to(8);
    }

    void pop_to(size_t size = 0) {
        while (this->fq_deque.size() > size) {
            this->fq_processor(this->fq_deque.front().get());
            this->fq_deque.pop_front();
        }
    }

    std::function<void(const T&)> fq_processor;
    std::deque<std::future<T>> fq_deque;
};

#endif
