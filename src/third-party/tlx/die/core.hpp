/*******************************************************************************
 * tlx/die/core.hpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2016-2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#ifndef TLX_DIE_CORE_HEADER
#define TLX_DIE_CORE_HEADER

#include <cstring>
#include <iomanip> // NOLINT(misc-include-cleaner)
#include <sstream> // NOLINT(misc-include-cleaner)
#include <stdexcept>
#include <string>

namespace tlx {

/******************************************************************************/
// die macros

//! die with message - either throw an exception or die via std::terminate()
void die_with_message(const std::string& msg);

//! die with message - either throw an exception or die via std::terminate()
void die_with_message(const char* msg, const char* file, size_t line);

//! die with message - either throw an exception or die via std::terminate()
void die_with_message(const std::string& msg, const char* file, size_t line);

//! Instead of std::terminate(), throw the output the message via an exception.
#define tlx_die_with_sstream(msg)                                              \
    do                                                                         \
    {                                                                          \
        std::ostringstream oss__;                                              \
        oss__ << msg << " @ " << __FILE__ << ':' << __LINE__;                  \
        ::tlx::die_with_message(oss__.str());                                  \
        std::terminate(); /* tell compiler this never returns */               \
    } while (false)

//! Instead of std::terminate(), throw the output the message via an exception.
#define tlx_die(msg)                                                           \
    do                                                                         \
    {                                                                          \
        tlx_die_with_sstream("DIE: " << msg);                                  \
    } while (false)

//! Exception thrown by die_with_message() if
class DieException : public std::runtime_error
{
public:
    explicit DieException(const std::string& message);
};

//! Switch between dying via std::terminate() and throwing an exception.
//! Alternatively define the macro TLX_DIE_WITH_EXCEPTION=1
bool set_die_with_exception(bool b);

/******************************************************************************/
// die_unless() and die_if()

//! Check condition X and die miserably if false. Same as assert() except this
//! is also active in Release mode.
#define tlx_die_unless(X)                                                      \
    do                                                                         \
    {                                                                          \
        if (!(X)) /* NOLINT(readability-simplify-boolean-expr) */              \
        {                                                                      \
            ::tlx::die_with_message("DIE: Assertion \"" #X "\" failed!",       \
                                    __FILE__, __LINE__);                       \
        }                                                                      \
    } while (false)

//! Check condition X and die miserably if true. Opposite of assert() except
//! this is also active in Release mode.
#define tlx_die_if(X)                                                          \
    do                                                                         \
    {                                                                          \
        if (X)                                                                 \
        {                                                                      \
            ::tlx::die_with_message("DIE: Assertion \"" #X "\" succeeded!",    \
                                    __FILE__, __LINE__);                       \
        }                                                                      \
    } while (false)

//! Check condition X and die miserably if false. Same as tlx_die_unless()
//! except the user additionally passes a message.
#define tlx_die_verbose_unless(X, msg)                                         \
    do                                                                         \
    {                                                                          \
        if (!(X)) /* NOLINT(readability-simplify-boolean-expr) */              \
        {                                                                      \
            tlx_die_with_sstream("DIE: Assertion \"" #X "\" failed!\n"         \
                                 << msg << '\n');                              \
        }                                                                      \
    } while (false)

//! Check condition X and die miserably if false. Same as tlx_die_if()
//! except the user additionally passes a message.
#define tlx_die_verbose_if(X, msg)                                             \
    do                                                                         \
    {                                                                          \
        if ((X))                                                               \
        {                                                                      \
            tlx_die_with_sstream("DIE: Assertion \"" #X "\" succeeded!\n"      \
                                 << msg << '\n');                              \
        }                                                                      \
    } while (false)

/******************************************************************************/
// die_unequal()

//! helper method to compare two values in die_unequal()
template <typename TypeA, typename TypeB>
inline bool die_equal_compare(TypeA a, TypeB b)
{
    return a == b;
}

template <>
inline bool die_equal_compare(const char* a, const char* b)
{
    // compare string contents
    return std::strcmp(a, b) == 0;
}

template <>
inline bool die_equal_compare(float a, float b)
{
    // special case for NAN
    return a != a ? b != b : a == b;
}

template <>
inline bool die_equal_compare(double a, double b)
{
    // special case for NAN
    return a != a ? b != b : a == b;
}

//! Check that X == Y or die miserably, but output the values of X and Y for
//! better debugging.
#define tlx_die_unequal(X, Y)                                                  \
    do                                                                         \
    {                                                                          \
        auto x__ = (X); /* NOLINT */                                           \
        auto y__ = (Y); /* NOLINT */                                           \
        if (!::tlx::die_equal_compare(x__, y__))                               \
        {                                                                      \
            tlx_die_with_sstream("DIE-UNEQUAL: " #X " != " #Y " : "            \
                                 "\""                                          \
                                 << x__ << "\" != \"" << y__ << "\"");         \
        }                                                                      \
    } while (false)

//! Check that X == Y or die miserably, but output the values of X and Y for
//! better debugging. Only active if NDEBUG is not defined.
#ifdef NDEBUG
#define tlx_assert_equal(X, Y)
#else
#define tlx_assert_equal(X, Y) die_unequal(X, Y)
#endif

//! Check that X == Y or die miserably, but output the values of X and Y for
//! better debugging. Same as tlx_die_unequal() except the user additionally
//! pass a message.
#define tlx_die_verbose_unequal(X, Y, msg)                                     \
    do                                                                         \
    {                                                                          \
        auto x__ = (X); /* NOLINT */                                           \
        auto y__ = (Y); /* NOLINT */                                           \
        if (!::tlx::die_equal_compare(x__, y__))                               \
        {                                                                      \
            tlx_die_with_sstream("DIE-UNEQUAL: " #X " != " #Y " : "            \
                                 "\""                                          \
                                 << x__ << "\" != \"" << y__ << "\"\n"         \
                                 << msg << '\n');                              \
        }                                                                      \
    } while (false)

/******************************************************************************/
// die_unequal_eps()

//! simple replacement for std::abs
template <typename Type>
inline Type die_unequal_eps_abs(const Type& t)
{
    return t < 0 ? -t : t;
}

//! helper method to compare two values in die_unequal_eps()
template <typename TypeA, typename TypeB>
inline bool die_equal_eps_compare(TypeA x, TypeB y, double eps)
{
    // special case for NAN
    return x != x ? y != y : die_unequal_eps_abs(x - y) <= eps;
}

//! Check that ABS(X - Y) <= eps or die miserably, but output the values of X
//! and Y for better debugging.
#define tlx_die_unequal_eps(X, Y, eps)                                         \
    do                                                                         \
    {                                                                          \
        auto x__ = (X); /* NOLINT */                                           \
        auto y__ = (Y); /* NOLINT */                                           \
        if (!::tlx::die_equal_eps_compare(x__, y__, eps))                      \
        {                                                                      \
            tlx_die("DIE-UNEQUAL-EPS: " #X " != " #Y " : "                     \
                    << std::setprecision(18) << "\"" << x__ << "\" != \""      \
                    << y__ << "\"");                                           \
        }                                                                      \
    } while (false)

//! Check that ABS(X - Y) <= eps or die miserably, but output the values of X
//! and Y for better debugging. Same as tlx_die_unequal_eps() except the user
//! additionally passes a message.
#define tlx_die_verbose_unequal_eps(X, Y, eps, msg)                            \
    do                                                                         \
    {                                                                          \
        auto x__ = (X); /* NOLINT */                                           \
        auto y__ = (Y); /* NOLINT */                                           \
        if (!::tlx::die_equal_eps_compare(x__, y__, eps))                      \
        {                                                                      \
            tlx_die("DIE-UNEQUAL-EPS: " #X " != " #Y " : "                     \
                    << std::setprecision(18) << "\"" << x__ << "\" != \""      \
                    << y__ << "\"\n"                                           \
                    << msg << '\n');                                           \
        }                                                                      \
    } while (false)

//! Check that ABS(X - Y) <= 0.000001 or die miserably, but output the values of
//! X and Y for better debugging.
#define tlx_die_unequal_eps6(X, Y) die_unequal_eps(X, Y, 1e-6)

//! Check that ABS(X - Y) <= 0.000001 or die miserably, but output the values of
//! X and Y for better debugging. Same as tlx_die_unequal_eps6() except the user
//! additionally passes a message.
#define tlx_die_verbose_unequal_eps6(X, Y, msg)                                \
    die_verbose_unequal_eps(X, Y, 1e-6, msg)

/******************************************************************************/
// die_equal()

//! Die miserably if X == Y, but first output the values of X and Y for better
//! debugging.
#define tlx_die_equal(X, Y)                                                    \
    do                                                                         \
    {                                                                          \
        auto x__ = (X); /* NOLINT */                                           \
        auto y__ = (Y); /* NOLINT */                                           \
        if (::tlx::die_equal_compare(x__, y__))                                \
        {                                                                      \
            tlx_die_with_sstream("DIE-EQUAL: " #X " == " #Y " : "              \
                                 "\""                                          \
                                 << x__ << "\" == \"" << y__ << "\"");         \
        }                                                                      \
    } while (false)

//! Die miserably if X == Y, but first output the values of X and Y for better
//! debugging. Only active if NDEBUG is not defined.
#ifdef NDEBUG
#define tlx_assert_unequal(X, Y)
#else
#define tlx_assert_unequal(X, Y) die_equal(X, Y)
#endif

//! Die miserably if X == Y, but first output the values of X and Y for better
//! debugging. Same as tlx_die_equal() except the user additionally passes a
//! message.
#define tlx_die_verbose_equal(X, Y, msg)                                       \
    do                                                                         \
    {                                                                          \
        auto x__ = (X); /* NOLINT */                                           \
        auto y__ = (Y); /* NOLINT */                                           \
        if (::tlx::die_equal_compare(x__, y__))                                \
        {                                                                      \
            tlx_die_with_sstream("DIE-EQUAL: " #X " == " #Y " : "              \
                                 "\""                                          \
                                 << x__ << "\" == \"" << y__ << "\"\n"         \
                                 << msg << '\n');                              \
        }                                                                      \
    } while (false)

/******************************************************************************/
// die_unless_throws()

//! Define to check that [code] throws and exception of given type
#define tlx_die_unless_throws(code, exception_type)                            \
    do                                                                         \
    {                                                                          \
        try                                                                    \
        {                                                                      \
            code;                                                              \
        }                                                                      \
        catch (const exception_type&)                                          \
        {                                                                      \
            break;                                                             \
        }                                                                      \
        ::tlx::die_with_message("DIE-UNLESS-THROWS: " #code                    \
                                " - NO EXCEPTION " #exception_type,            \
                                __FILE__, __LINE__);                           \
    } while (false)

} // namespace tlx

#endif // !TLX_DIE_CORE_HEADER

/******************************************************************************/
