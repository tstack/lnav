// Copyright 2017 Elias Kosunen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is a part of scnlib:
//     https://github.com/eliaskosunen/scnlib

#ifndef SCN_DETAIL_RESULT_H
#define SCN_DETAIL_RESULT_H

#include "../util/expected.h"
#include "error.h"
#include "range.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    /**
     * Base class for the result type returned by most scanning functions
     * (except for \ref scan_value). \ref scn::detail::scan_result_base inherits
     * either from this class or \ref expected.
     */
    struct wrapped_error {
        wrapped_error() = default;
        wrapped_error(::scn::error e) : err(e) {}

        /// Get underlying error
        SCN_NODISCARD ::scn::error error() const
        {
            return err;
        }

        /// Did the operation succeed -- true means success
        explicit operator bool() const
        {
            return err.operator bool();
        }

        ::scn::error err{};
    };

    namespace detail {
        template <typename Base>
        class scan_result_base_wrapper : public Base {
        public:
            scan_result_base_wrapper(Base&& b) : Base(SCN_MOVE(b)) {}

        protected:
            void set_base(const Base& b)
            {
                static_cast<Base&>(*this) = b;
            }
            void set_base(Base&& b)
            {
                static_cast<Base&>(*this) = SCN_MOVE(b);
            }
        };

        SCN_CLANG_PUSH
        SCN_CLANG_IGNORE("-Wdocumentation-unknown-command")

        /// @{

        /**
         * Type returned by scanning functions.
         * Contains an error (inherits from it: for \ref error, that's \ref
         * wrapped_error; with \ref scan_value, inherits from \ref expected),
         * and the leftover range after scanning.
         *
         * The leftover range may reference the range given to the scanning
         * function. Please take the necessary measures to make sure that the
         * original range outlives the leftover range. Alternatively, if
         * possible for your specific range type, call the \ref reconstruct()
         * member function to get a new, independent range.
         */
        template <typename WrappedRange, typename Base>
        class scan_result_base : public scan_result_base_wrapper<Base> {
        public:
            using wrapped_range_type = WrappedRange;
            using base_type = scan_result_base_wrapper<Base>;

            using range_type = typename wrapped_range_type::range_type;
            using iterator = typename wrapped_range_type::iterator;
            using sentinel = typename wrapped_range_type::sentinel;
            using char_type = typename wrapped_range_type::char_type;

            scan_result_base(Base&& b, wrapped_range_type&& r)
                : base_type(SCN_MOVE(b)), m_range(SCN_MOVE(r))
            {
            }

            /// Beginning of the leftover range
            iterator begin() const noexcept
            {
                return m_range.begin();
            }
            SCN_GCC_PUSH
            SCN_GCC_IGNORE("-Wnoexcept")
            // Mitigate problem where Doxygen would think that SCN_GCC_PUSH was
            // a part of the definition of end()
        public:
            /// End of the leftover range
            sentinel end() const
                noexcept(noexcept(SCN_DECLVAL(wrapped_range_type).end()))
            {
                return m_range.end();
            }

            /// Whether the leftover range is empty
            bool empty() const
                noexcept(noexcept(SCN_DECLVAL(wrapped_range_type).end()))
            {
                return begin() == end();
            }
            SCN_GCC_POP
            // See above at SCN_GCC_PUSH
        public:
            /// A subrange pointing to the leftover range
            ranges::subrange<iterator, sentinel> subrange() const
            {
                return {begin(), end()};
            }

            /**
             * Leftover range.
             * If the leftover range is used to scan a new value, this member
             * function should be used.
             *
             * \see range_wrapper
             */
            wrapped_range_type& range() &
            {
                return m_range;
            }
            /// \copydoc range()
            const wrapped_range_type& range() const&
            {
                return m_range;
            }
            /// \copydoc range()
            wrapped_range_type range() &&
            {
                return SCN_MOVE(m_range);
            }

            /**
             * \defgroup range_as_range Contiguous leftover range convertors
             *
             * These member functions enable more convenient use of the
             * leftover range for non-scnlib use cases. The range must be
             * contiguous. The leftover range is not advanced, and can still be
             * used.
             *
             * @{
             */

            /**
             * \ingroup range_as_range
             * Return a view into the leftover range as a \c string_view.
             * Operations done to the leftover range after a call to this may
             * cause issues with iterator invalidation. The returned range will
             * reference to the leftover range, so be wary of
             * use-after-free-problems.
             */
            template <
                typename R = wrapped_range_type,
                typename = typename std::enable_if<R::is_contiguous>::type>
            basic_string_view<char_type> range_as_string_view() const
            {
                return {m_range.data(),
                        static_cast<std::size_t>(m_range.size())};
            }
            /**
             * \ingroup range_as_range
             * Return a view into the leftover range as a \c span.
             * Operations done to the leftover range after a call to this may
             * cause issues with iterator invalidation. The returned range will
             * reference to the leftover range, so be wary of
             * use-after-free-problems.
             */
            template <
                typename R = wrapped_range_type,
                typename = typename std::enable_if<R::is_contiguous>::type>
            span<const char_type> range_as_span() const
            {
                return {m_range.data(),
                        static_cast<std::size_t>(m_range.size())};
            }
            /**
             * \ingroup range_as_range
             * Return the leftover range as a string. The contents are copied
             * into the string, so using this will not lead to lifetime issues.
             */
            template <
                typename R = wrapped_range_type,
                typename = typename std::enable_if<R::is_contiguous>::type>
            std::basic_string<char_type> range_as_string() const
            {
                return {m_range.data(),
                        static_cast<std::size_t>(m_range.size())};
            }
            /// @}

        protected:
            wrapped_range_type m_range;

        private:
            /// \publicsection

            /**
             * Reconstructs a range of the original type, independent of the
             * leftover range, beginning from \ref begin and ending in \ref end.
             *
             * Compiles only if range is reconstructible.
             */
            template <typename R = typename WrappedRange::range_type>
            R reconstruct() const;
        };

        template <typename WrappedRange, typename Base>
        class intermediary_scan_result
            : public scan_result_base<WrappedRange, Base> {
        public:
            using base_type = scan_result_base<WrappedRange, Base>;

            intermediary_scan_result(Base&& b, WrappedRange&& r)
                : base_type(SCN_MOVE(b), SCN_MOVE(r))
            {
            }

            template <typename R = WrappedRange>
            void reconstruct() const
            {
                static_assert(
                    dependent_false<R>::value,
                    "Cannot call .reconstruct() on intermediary_scan_result. "
                    "Assign this value to a previous result value returned by "
                    "a scanning function or make_result (type: "
                    "reconstructed_scan_result or "
                    "non_reconstructed_scan_result) ");
            }
        };
        template <typename WrappedRange, typename Base>
        class reconstructed_scan_result
            : public intermediary_scan_result<WrappedRange, Base> {
        public:
            using unwrapped_range_type = typename WrappedRange::range_type;
            using base_type = intermediary_scan_result<WrappedRange, Base>;

            reconstructed_scan_result(Base&& b, WrappedRange&& r)
                : base_type(SCN_MOVE(b), SCN_MOVE(r))
            {
            }

            reconstructed_scan_result& operator=(
                const intermediary_scan_result<WrappedRange, Base>& other)
            {
                this->set_base(other);
                this->m_range = other.range();
                return *this;
            }
            reconstructed_scan_result& operator=(
                intermediary_scan_result<WrappedRange, Base>&& other)
            {
                this->set_base(other);
                this->m_range = other.range();
                return *this;
            }

            unwrapped_range_type reconstruct() const
            {
                return this->range().range_underlying();
            }
        };
        template <typename WrappedRange, typename UnwrappedRange, typename Base>
        class non_reconstructed_scan_result
            : public intermediary_scan_result<WrappedRange, Base> {
        public:
            using unwrapped_range_type = UnwrappedRange;
            using base_type = intermediary_scan_result<WrappedRange, Base>;

            non_reconstructed_scan_result(Base&& b, WrappedRange&& r)
                : base_type(SCN_MOVE(b), SCN_MOVE(r))
            {
            }

            non_reconstructed_scan_result& operator=(
                const intermediary_scan_result<WrappedRange, Base>& other)
            {
                this->set_base(other);
                this->m_range = other.range();
                return *this;
            }
            non_reconstructed_scan_result& operator=(
                intermediary_scan_result<WrappedRange, Base>&& other)
            {
                this->set_base(other);
                this->m_range = other.range();
                return *this;
            }

            template <typename R = unwrapped_range_type>
            R reconstruct() const
            {
                return ::scn::detail::reconstruct(reconstruct_tag<R>{},
                                                  this->begin(), this->end());
            }
        };

        /// @}

        // -Wdocumentation-unknown-command
        SCN_CLANG_PUSH

        template <typename T>
        struct range_tag {
        };

        namespace _wrap_result {
            struct fn {
            private:
                // Range = range_wrapper<ref>&
                template <typename Error, typename Range>
                static auto impl(Error e,
                                 range_tag<range_wrapper<Range&>&>,
                                 range_wrapper<Range&>&& range,
                                 priority_tag<5>) noexcept
                    -> intermediary_scan_result<range_wrapper<Range&>, Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }
                // Range = const range_wrapper<ref>&
                template <typename Error, typename Range>
                static auto impl(Error e,
                                 range_tag<const range_wrapper<Range&>&>,
                                 range_wrapper<Range&>&& range,
                                 priority_tag<5>) noexcept
                    -> intermediary_scan_result<range_wrapper<Range&>, Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }
                // Range = range_wrapper<ref>&&
                template <typename Error, typename Range>
                static auto impl(Error e,
                                 range_tag<range_wrapper<Range&>>,
                                 range_wrapper<Range&>&& range,
                                 priority_tag<5>) noexcept
                    -> intermediary_scan_result<range_wrapper<Range&>, Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }

                // Range = range_wrapper<non-ref>&
                template <typename Error, typename Range>
                static auto impl(Error e,
                                 range_tag<range_wrapper<Range>&>,
                                 range_wrapper<Range>&& range,
                                 priority_tag<4>) noexcept
                    -> intermediary_scan_result<range_wrapper<Range>, Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }
                // Range = const range_wrapper<non-ref>&
                template <typename Error, typename Range>
                static auto impl(Error e,
                                 range_tag<const range_wrapper<Range>&>,
                                 range_wrapper<Range>&& range,
                                 priority_tag<4>) noexcept
                    -> intermediary_scan_result<range_wrapper<Range>, Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }
                // Range = range_wrapper<non-ref>&&
                template <typename Error, typename Range>
                static auto impl(Error e,
                                 range_tag<range_wrapper<Range>>,
                                 range_wrapper<Range>&& range,
                                 priority_tag<4>) noexcept
                    -> intermediary_scan_result<range_wrapper<Range>, Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }

                // string literals are wonky
                template <typename Error,
                          typename CharT,
                          size_t N,
                          typename NoCVRef = remove_cvref_t<CharT>>
                static auto impl(
                    Error e,
                    range_tag<CharT (&)[N]>,
                    range_wrapper<basic_string_view<NoCVRef>>&& range,
                    priority_tag<3>) noexcept
                    -> reconstructed_scan_result<
                        range_wrapper<basic_string_view<NoCVRef>>,
                        Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)
                                             .template reconstruct_and_rewrap<
                                                 basic_string_view<NoCVRef>>()};
                }

                // (const) InputRange&: View + Reconstructible
                // wrapped<any>
                template <typename Error,
                          typename InputRange,
                          typename InnerWrappedRange,
                          typename InputRangeNoConst =
                              typename std::remove_const<InputRange>::type,
                          typename = typename std::enable_if<SCN_CHECK_CONCEPT(
                              ranges::view<InputRangeNoConst>)>::type>
                static auto impl(Error e,
                                 range_tag<InputRange&>,
                                 range_wrapper<InnerWrappedRange>&& range,
                                 priority_tag<2>) noexcept
                    -> reconstructed_scan_result<
                        decltype(SCN_MOVE(range)
                                     .template reconstruct_and_rewrap<
                                         InputRangeNoConst>()),
                        Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)
                                             .template reconstruct_and_rewrap<
                                                 InputRangeNoConst>()};
                }

                // (const) InputRange&: other
                // wrapped<any>
                template <typename Error,
                          typename InputRange,
                          typename InnerWrappedRange>
                static auto impl(Error e,
                                 range_tag<InputRange&>,
                                 range_wrapper<InnerWrappedRange>&& range,
                                 priority_tag<1>) noexcept
                    -> non_reconstructed_scan_result<
                        range_wrapper<InnerWrappedRange>,
                        typename std::remove_const<InputRange>::type,
                        Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }

                // InputRange&&: View + Reconstructible
                // wrapped<non-ref>
                template <typename Error,
                          typename InputRange,
                          typename InnerWrappedRange,
                          typename InputRangeNoConst =
                              typename std::remove_const<InputRange>::type,
                          typename = typename std::enable_if<SCN_CHECK_CONCEPT(
                              ranges::view<InputRangeNoConst>)>::type>
                static auto impl(Error e,
                                 range_tag<InputRange>,
                                 range_wrapper<InnerWrappedRange>&& range,
                                 priority_tag<1>) noexcept
                    -> reconstructed_scan_result<
                        decltype(SCN_MOVE(range)
                                     .template reconstruct_and_rewrap<
                                         InputRangeNoConst>()),
                        Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)
                                             .template reconstruct_and_rewrap<
                                                 InputRangeNoConst>()};
                }

                // InputRange&&: other
                // wrapped<non-ref>
                template <typename Error,
                          typename InputRange,
                          typename InnerWrappedRange>
                static auto impl(Error e,
                                 range_tag<InputRange>,
                                 range_wrapper<InnerWrappedRange>&& range,
                                 priority_tag<0>) noexcept
                    -> non_reconstructed_scan_result<
                        range_wrapper<InputRange>,
                        typename std::remove_const<InputRange>::type,
                        Error>
                {
                    return {SCN_MOVE(e), SCN_MOVE(range)};
                }

#if 0
                    // InputRange&&
                // wrapped<ref>
                template <typename Error,
                          typename InputRange,
                          typename InnerWrappedRange,
                          typename NoRef = typename std::remove_reference<
                              InnerWrappedRange>::type>
                static auto impl(Error e,
                                 range_tag<InputRange>,
                                 range_wrapper<InnerWrappedRange&>&& range,
                                 priority_tag<0>) noexcept
                    -> reconstructed_scan_result<range_wrapper<NoRef>, Error>
                {
                    return {SCN_MOVE(e),
                            SCN_MOVE(range)
                                .template rewrap_and_reconstruct<NoRef>()};
                }
#endif

            public:
                template <typename Error,
                          typename InputRange,
                          typename InnerWrappedRange>
                auto operator()(Error e,
                                range_tag<InputRange> tag,
                                range_wrapper<InnerWrappedRange>&& range) const
                    noexcept(noexcept(impl(SCN_MOVE(e),
                                           tag,
                                           SCN_MOVE(range),
                                           priority_tag<5>{})))
                        -> decltype(impl(SCN_MOVE(e),
                                         tag,
                                         SCN_MOVE(range),
                                         priority_tag<5>{}))
                {
                    static_assert(SCN_CHECK_CONCEPT(ranges::range<InputRange>),
                                  "Input needs to be a Range");
                    return impl(SCN_MOVE(e), tag, SCN_MOVE(range),
                                priority_tag<5>{});
                }
            };
        }  // namespace _wrap_result
        namespace {
            static constexpr auto& wrap_result =
                static_const<_wrap_result::fn>::value;
        }

        template <typename Error, typename InputRange, typename WrappedRange>
        struct result_type_for {
            using type =
                decltype(wrap_result(SCN_DECLVAL(Error &&),
                                     SCN_DECLVAL(range_tag<InputRange>),
                                     SCN_DECLVAL(WrappedRange&&)));
        };
        template <typename Error, typename InputRange, typename WrappedRange>
        using result_type_for_t =
            typename result_type_for<Error, InputRange, WrappedRange>::type;
    }  // namespace detail

    /**
     * Create a result object for range \c Range.
     * Useful if one wishes to scan from the same range in a loop.
     *
     * \code{.cpp}
     * auto source = ...;
     * auto result = make_result(source);
     * // scan until failure (no more `int`s, or EOF)
     * while (result) {
     *   int i;
     *   result = scn::scan(result.range(), "{}", i);
     *   // use i
     * }
     * // see result for why we exited the loop
     * \endcode
     *
     * \c Error template parameter can be used to customize the error type for
     * the result object. By default, it's \ref wrapped_error, which is what
     * most of the scanning functions use. For \c scan_value, use \c
     * expected<T>:
     *
     * \code{.cpp}
     * auto result = make_result<scn::expected<int>>(source);
     * while (result) {
     *   result = scn::scan_value<int>(result.range(), "{}");
     *   // use result.value()
     * }
     * \endcode
     */
    template <typename Error = wrapped_error, typename Range>
    auto make_result(Range&& r)
        -> detail::result_type_for_t<Error, Range, range_wrapper_for_t<Range>>
    {
        return detail::wrap_result(Error{}, detail::range_tag<Range>{},
                                   wrap(r));
    }

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_RESULT_H
