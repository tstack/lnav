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

#ifndef SCN_RANGES_RANGES_H
#define SCN_RANGES_RANGES_H

#include "../detail/config.h"

#ifndef SCN_USE_STD_RANGES

#if SCN_HAS_CONCEPTS && SCN_HAS_RANGES
#define SCN_USE_STD_RANGES 1
#else
#define SCN_USE_STD_RANGES 0
#endif

#endif  // !defined(SCN_USE_STD_RANGES)

#if SCN_USE_STD_RANGES
#include "std_impl.h"
#define SCN_RANGES_NAMESPACE ::scn::std_ranges
#else
#include "custom_impl.h"
#define SCN_RANGES_NAMESPACE ::scn::custom_ranges
#endif

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace ranges = SCN_RANGES_NAMESPACE;

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_RANGES_RANGES_H
