/**
 * @file mutableref.h
 * @author L.-C. C.
 * @brief 
 * @version 0.1
 * @date 2020-01-03
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#pragma once

#include <utility>

namespace safe
{
	namespace impl
	{
		/**
		 * @brief A helper class that defines a member variable of type
		 * Type. The variable is defined "mutable Type" if Type is not a
		 * reference, the variable is "Type&" if Type is a reference.
		 * 
		 * @tparam Type The type of the variable to define.
		 */
		template<typename Type>
		struct MutableIfNotReference
		{
			/// Mutable Type object.
			mutable Type get;
		};
		template<typename Type>
		struct MutableIfNotReference<Type&>
		{
			/// Reference to a Type object.
			Type& get;
		};
	} // namespace impl
} // namespace safe