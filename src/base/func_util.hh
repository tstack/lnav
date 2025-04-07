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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lnav_func_util_hh
#define lnav_func_util_hh

#include <functional>
#include <utility>

#include "progress.hh"

template<typename F, typename FrontArg>
decltype(auto)
bind_mem(F&& f, FrontArg&& frontArg)
{
    return [f = std::forward<F>(f),
            frontArg = std::forward<FrontArg>(frontArg)](auto&&... backArgs) {
        return (frontArg->*f)(std::forward<decltype(backArgs)>(backArgs)...);
    };
}

struct noop_func {
    struct anything {
        template<class T>
        operator T()
        {
            return {};
        }
        // optional reference support.  Somewhat evil.
        template<class T>
        operator T&() const
        {
            static T t{};
            return t;
        }
    };
    template<class... Args>
    anything operator()(Args&&...) const
    {
        return {};
    }
};

namespace lnav::func {

enum class op_type {
    blocking,
    interactive,
};

class scoped_cb {
public:
    using callback_type = std::function<progress_result_t(op_type)>;

    class guard {
    public:
        explicit guard(scoped_cb* owner) : g_owner(owner) {}

        guard(const guard&) = delete;
        guard& operator=(const guard&) = delete;

        guard(guard&& gu) noexcept : g_owner(std::exchange(gu.g_owner, nullptr))
        {
        }

        guard& operator=(guard&& gu) noexcept
        {
            this->g_owner = std::exchange(gu.g_owner, nullptr);
            return *this;
        }

        ~guard()
        {
            if (this->g_owner != nullptr) {
                this->g_owner->s_callback = {};
            }
        }

    private:
        scoped_cb* g_owner;
    };

    guard install(callback_type cb)
    {
        this->s_callback = std::move(cb);

        return guard{this};
    }

    progress_result_t operator()(op_type ot) const
    {
        if (s_callback) {
            return s_callback(ot);
        }

        return progress_result_t::ok;
    }

private:
    callback_type s_callback;
};

template<typename Fn,
         typename... Args,
         std::enable_if_t<std::is_member_pointer<std::decay_t<Fn>>{}, int> = 0>
constexpr decltype(auto)
invoke(Fn&& f, Args&&... args) noexcept(
    noexcept(std::mem_fn(f)(std::forward<Args>(args)...)))
{
    return std::mem_fn(f)(std::forward<Args>(args)...);
}

template<typename Fn,
         typename... Args,
         std::enable_if_t<!std::is_member_pointer<std::decay_t<Fn>>{}, int> = 0>
constexpr decltype(auto)
invoke(Fn&& f, Args&&... args) noexcept(
    noexcept(std::forward<Fn>(f)(std::forward<Args>(args)...)))
{
    return std::forward<Fn>(f)(std::forward<Args>(args)...);
}

template<class F, class... Args>
struct is_invocable {
    template<typename U, typename Obj, typename... FuncArgs>
    static auto test(U&& p)
        -> decltype((std::declval<Obj>().*p)(std::declval<FuncArgs>()...),
                    void(),
                    std::true_type());
    template<typename U, typename... FuncArgs>
    static auto test(U* p) -> decltype((*p)(std::declval<FuncArgs>()...),
                                       void(),
                                       std::true_type());
    template<typename U, typename... FuncArgs>
    static auto test(...) -> decltype(std::false_type());

    static constexpr bool value = decltype(test<F, Args...>(0))::value;
};

}  // namespace lnav::func

#endif
