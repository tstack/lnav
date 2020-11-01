/**
 * @file accessmode.h
 * @author L.-C. C.
 * @brief 
 * @version 0.1
 * @date 2019-01-20
 * 
 * @copyright Copyright (c) 2019
 * 
 */

#pragma once

#include <mutex>
#if __cplusplus >= 201402L
#include <shared_mutex>
#endif // __cplusplus >= 201402L

namespace safe
{
	enum class AccessMode
	{
		ReadOnly,
		ReadWrite
	};

	template<typename LockType>
	struct AccessTraits
	{
		static constexpr bool IsReadOnly = false;
	};
	template<typename MutexType>
	struct AccessTraits<std::lock_guard<MutexType>>
	{
		static constexpr bool IsReadOnly = false;
	};
	template<typename MutexType>
	struct AccessTraits<std::unique_lock<MutexType>>
	{
		static constexpr bool IsReadOnly = false;
	};
#if __cplusplus >= 201402L
	template<typename MutexType>
	struct AccessTraits<std::shared_lock<MutexType>>
	{
		static constexpr bool IsReadOnly = true;
	};
#endif // __cplusplus >= 201402L
} // namespace safe