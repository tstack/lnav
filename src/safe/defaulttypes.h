/**
 * @file defaulttypes.h
 * @author L.-C. C.
 * @brief 
 * @version 0.1
 * @date 2020-01-29
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#pragma once

#include <mutex>

namespace safe
{
	using DefaultMutex = std::mutex;
	template<typename MutexType>
	using DefaultReadOnlyLock = std::lock_guard<MutexType>;
	template<typename MutexType>
	using DefaultReadWriteLock = std::lock_guard<MutexType>;
}  // namespace safe