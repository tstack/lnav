/**
 * @file safe.h
 * @author L.-C. C.
 * @brief
 * @version 0.1
 * @date 2018-09-21
 *
 * @copyright Copyright (c) 2018
 *
 */

#pragma once

#include <type_traits>
#include <utility>

#include "accessmode.h"
#include "defaulttypes.h"
#include "mutableref.h"

#if __cplusplus >= 201703L
#    define EXPLICIT_IF_CPP17                         explicit
#    define EXPLICITLY_CONSTRUCT_RETURN_TYPE_IF_CPP17 ReturnType
#else
#    define EXPLICIT_IF_CPP17
#    define EXPLICITLY_CONSTRUCT_RETURN_TYPE_IF_CPP17
#endif

namespace safe {
/**
 * @brief Use this tag to default construct the mutex when constructing a
 * Safe object.
 */
struct DefaultConstructMutex {};
static constexpr DefaultConstructMutex default_construct_mutex;

/**
 * @brief Wraps a value together with a mutex.
 *
 * @tparam ValueType The type of the value to protect.
 * @tparam MutexType The type of the mutex.
 */
template<typename ValueType, typename MutexType = DefaultMutex>
class Safe {
private:
    /// Type ValueType with reference removed, if present
    using RemoveRefValueType = typename std::remove_reference<ValueType>::type;
    /// Type MutexType with reference removed, if present
    using RemoveRefMutexType = typename std::remove_reference<MutexType>::type;

    /**
     * @brief Manages a mutex and gives pointer-like access to a value
     * object.
     *
     * @tparam LockType The type of the lock object that manages the
     * mutex, example: std::lock_guard.
     * @tparam Mode Determines the access mode of the Access
     * object. Can be either AccessMode::ReadOnly or
     * AccessMode::ReadWrite.
     */
    template<template<typename> class LockType, AccessMode Mode>
    class Access {
        // Make sure AccessMode is ReadOnly if a read-only lock is used
        static_assert(
            !(AccessTraits<LockType<RemoveRefMutexType>>::IsReadOnly
              && Mode == AccessMode::ReadWrite),
            "Cannot have ReadWrite access mode with ReadOnly lock. Check the "
            "value of AccessTraits<LockType>::IsReadOnly if it exists.");

        /// ValueType with const qualifier if AccessMode is ReadOnly.
        using ConstIfReadOnlyValueType =
            typename std::conditional<Mode == AccessMode::ReadOnly,
                                      const RemoveRefValueType,
                                      RemoveRefValueType>::type;

    public:
        /// Pointer-to-const ValueType
        using ConstPointerType = const ConstIfReadOnlyValueType*;
        /// Pointer-to-const ValueType if Mode is ReadOnly, pointer to ValueType
        /// otherwise.
        using PointerType = ConstIfReadOnlyValueType*;
        /// Reference-to-const ValueType
        using ConstReferenceType = const ConstIfReadOnlyValueType&;
        /// Reference-to-const ValueType if Mode is ReadOnly, reference to
        /// ValueType otherwise.
        using ReferenceType = ConstIfReadOnlyValueType&;

        /**
         * @brief Construct an Access object from a possibly const
         * reference to the value object and any additionnal argument
         * needed to construct the Lock object.
         *
         * @tparam LockArgs Deduced from lockArgs.
         * @param value Reference to the value.
         * @param lockArgs Arguments needed to construct the lock object.
         */
        template<typename... OtherLockArgs>
        EXPLICIT_IF_CPP17 Access(ReferenceType value,
                                 MutexType& mutex,
                                 OtherLockArgs&&... otherLockArgs)
            : lock(mutex, std::forward<OtherLockArgs>(otherLockArgs)...),
              m_value(value)
        {
        }

        /**
         * @brief Construct a read-only Access object from a const
         * safe::Safe object and any additionnal argument needed to
         * construct the Lock object.
         *
         * If needed, you can provide additionnal arguments to construct
         * the lock object (such as std::adopt_lock). The mutex from the
         * safe::Locakble object is already passed to the lock object's
         * constructor though, you must not provide it.
         *
         * @tparam OtherLockArgs Deduced from otherLockArgs.
         * @param safe The const Safe object to give protected access to.
         * @param otherLockArgs Other arguments needed to construct the lock
         * object.
         */
        template<typename... OtherLockArgs>
        EXPLICIT_IF_CPP17 Access(const Safe& safe,
                                 OtherLockArgs&&... otherLockArgs)
            : Access(safe.m_value,
                     safe.m_mutex.get,
                     std::forward<OtherLockArgs>(otherLockArgs)...)
        {
        }

        /**
         * @brief Construct a read-write Access object from a
         * safe::Safe object and any additionnal argument needed to
         * construct the Lock object.
         *
         * If needed, you can provide additionnal arguments to construct
         * the lock object (such as std::adopt_lock). The mutex from the
         * safe object is already passed to the lock object's constructor
         * though, you must not provide it.
         *
         * @tparam OtherLockArgs Deduced from otherLockArgs.
         * @param safe The Safe object to give protected access to.
         * @param otherLockArgs Other arguments needed to construct the lock
         * object.
         */
        template<typename... OtherLockArgs>
        EXPLICIT_IF_CPP17 Access(Safe& safe, OtherLockArgs&&... otherLockArgs)
            : Access(safe.m_value,
                     safe.m_mutex.get,
                     std::forward<OtherLockArgs>(otherLockArgs)...)
        {
        }

        /**
         * @brief Construct an Access object from another one.
         * OtherLockType must implement release() like std::unique_lock
         * does.
         *
         * @tparam OtherLockType Deduced from otherAccess.
         * @tparam OtherMode Deduced from otherAccess.
         * @tparam OtherLockArgs Deduced from otherLockArgs.
         * @param otherAccess The Access object to construct from.
         * @param otherLockArgs Other arguments needed to construct the lock
         * object.
         */
        template<template<typename> class OtherLockType,
                 AccessMode OtherMode,
                 typename... OtherLockArgs>
        EXPLICIT_IF_CPP17 Access(Access<OtherLockType, OtherMode>& otherAccess,
                                 OtherLockArgs&&... otherLockArgs)
            : Access(*otherAccess,
                     *otherAccess.lock.release(),
                     std::adopt_lock,
                     std::forward<OtherLockArgs>(otherLockArgs)...)
        {
            static_assert(
                OtherMode == AccessMode::ReadWrite || OtherMode == Mode,
                "Cannot construct a ReadWrite Access object from a ReadOnly "
                "one!");
        }

        /**
         * @brief Const accessor to the value.
         * @return ConstPointerType Const pointer to the protected value.
         */
        ConstPointerType operator->() const noexcept { return &m_value; }

        /**
         * @brief Accessor to the value.
         * @return ValuePointerType Pointer to the protected value.
         */
        PointerType operator->() noexcept { return &m_value; }

        /**
         * @brief Const accessor to the value.
         * @return ConstValueReferenceType Const reference to the protected
         * value.
         */
        ConstReferenceType operator*() const noexcept { return m_value; }

        /**
         * @brief Accessor to the value.
         * @return ValueReferenceType Reference to the protected.
         */
        ReferenceType operator*() noexcept { return m_value; }

        /// The lock that manages the mutex.
        mutable LockType<RemoveRefMutexType> lock;

    private:
        /// The protected value.
        ReferenceType m_value;
    };

    /// Reference-to-const ValueType.
    using ConstValueReferenceType = const RemoveRefValueType&;
    /// Reference to ValueType.
    using ValueReferenceType = RemoveRefValueType&;
    /// Reference to MutexType.
    using MutexReferenceType = RemoveRefMutexType&;

public:
    /// Aliases to ReadAccess and WriteAccess classes for this Safe class.
    template<template<typename> class LockType = DefaultReadOnlyLock>
    using ReadAccess = Access<LockType, AccessMode::ReadOnly>;
    template<template<typename> class LockType = DefaultReadWriteLock>
    using WriteAccess = Access<LockType, AccessMode::ReadWrite>;

    /**
     * @brief Construct a Safe object
     */
    Safe() = default;

    /**
     * @brief Construct a Safe object with default construction of
     * the mutex and perfect forwarding of the other arguments to
     * construct the value object.
     *
     * @tparam ValueArgs Deduced from valueArgs.
     * @param valueArgs Perfect forwarding arguments to construct the value
     * object.
     * @param tag Indicates that the mutex should be default constructed.
     */
    template<typename... ValueArgs>
    explicit Safe(DefaultConstructMutex, ValueArgs&&... valueArgs)
        : m_mutex(), m_value(std::forward<ValueArgs>(valueArgs)...)
    {
    }
    /**
     * @brief Construct a Safe object, forwarding the first
     * argument to construct the mutex and the other arguments to
     * construct the value object.
     *
     * @tparam MutexArg Deduced from mutexArg.
     * @tparam ValueArgs Deduced from valueArgs.
     * @param valueArgs Perfect forwarding arguments to construct the
     * value object.
     * @param mutexArg Perfect forwarding argument to construct the
     * mutex object.
     */
    template<typename MutexArg, typename... ValueArgs>
    explicit Safe(MutexArg&& mutexArg, ValueArgs&&... valueArgs)
        : m_mutex{std::forward<MutexArg>(mutexArg)},
          m_value(std::forward<ValueArgs>(valueArgs)...)
    {
    }

    /// Delete all copy/move construction/assignment, as these operations
    /// require locking the mutex under the covers.
    /// Use copy(), assign() and other defined constructors to get the behavior
    /// you need with an explicit syntax.
    Safe(const Safe&) = delete;
    Safe(Safe&&) = delete;
    Safe& operator=(const Safe&) = delete;
    Safe& operator=(Safe&&) = delete;

    template<template<typename> class LockType = DefaultReadOnlyLock,
             typename... LockArgs>
    ReadAccess<LockType> readAccess(LockArgs&&... lockArgs) const
    {
        using ReturnType = ReadAccess<LockType>;
        return EXPLICITLY_CONSTRUCT_RETURN_TYPE_IF_CPP17{
            *this, std::forward<LockArgs>(lockArgs)...};
    }

    template<template<typename> class LockType = DefaultReadWriteLock,
             typename... LockArgs>
    WriteAccess<LockType> writeAccess(LockArgs&&... lockArgs)
    {
        using ReturnType = WriteAccess<LockType>;
        return EXPLICITLY_CONSTRUCT_RETURN_TYPE_IF_CPP17{
            *this, std::forward<LockArgs>(lockArgs)...};
    }

    template<template<typename> class LockType = DefaultReadOnlyLock,
             typename... LockArgs>
    RemoveRefValueType copy(LockArgs&&... lockArgs) const
    {
        return *readAccess<LockType>(std::forward<LockArgs>(lockArgs)...);
    }

    template<template<typename> class LockType = DefaultReadWriteLock,
             typename... LockArgs>
    void assign(ConstValueReferenceType value, LockArgs&&... lockArgs)
    {
        *writeAccess<LockType>(std::forward<LockArgs>(lockArgs)...) = value;
    }
    template<template<typename> class LockType = DefaultReadWriteLock,
             typename... LockArgs>
    void assign(RemoveRefValueType&& value, LockArgs&&... lockArgs)
    {
        *writeAccess<LockType>(std::forward<LockArgs>(lockArgs)...)
            = std::move(value);
    }

    /**
     * @brief Unsafe const accessor to the value. If you use this
     * function, you exit the realm of safe!
     *
     * @return ConstValueReferenceType Const reference to the value
     * object.
     */
    ConstValueReferenceType unsafe() const noexcept { return m_value; }
    /**
     * @brief Unsafe accessor to the value. If you use this function,
     * you exit the realm of safe!
     *
     * @return ValueReferenceType Reference to the value object.
     */
    ValueReferenceType unsafe() noexcept { return m_value; }

    /**
     * @brief Accessor to the mutex.
     *
     * @return MutexReferenceType Reference to the mutex.
     */
    MutexReferenceType mutex() const noexcept { return m_mutex.get; }

private:
    /// The helper object that holds the mutable mutex, or a reference to a
    /// mutex.
    impl::MutableIfNotReference<MutexType> m_mutex;
    /// The value to protect.
    ValueType m_value;
};

/**
 * @brief Type alias for read-only Access.
 *
 * @tparam SafeType The type of Safe object to give read-only access to.
 * @tparam LockType=DefaultReadOnlyLock The type of lock.
 */
template<typename SafeType,
         template<typename> class LockType = DefaultReadOnlyLock>
using ReadAccess = typename SafeType::template ReadAccess<LockType>;

/**
 * @brief Type alias for read-write Access.
 *
 * @tparam SafeType The type of Safe object to give read-write access to.
 * @tparam LockType=DefaultReadWriteLock The type of lock.
 */
template<typename SafeType,
         template<typename> class LockType = DefaultReadWriteLock>
using WriteAccess = typename SafeType::template WriteAccess<LockType>;
}  // namespace safe

#undef EXPLICIT_IF_CPP17
#undef EXPLICITLY_CONSTRUCT_RETURN_TYPE_IF_CPP17
