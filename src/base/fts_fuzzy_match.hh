// LICENSE
//
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// VERSION
//   0.2.0  (2017-02-18)  Scored matches perform exhaustive search for best
//   score 0.1.0  (2016-03-28)  Initial release
//
// AUTHOR
//   Forrest Smith
//
// NOTES
//   Compiling
//     You MUST add '#define FTS_FUZZY_MATCH_IMPLEMENTATION' before including
//     this header in ONE source file to create implementation.
//
//   fuzzy_match_simple(...)
//     Returns true if each character in pattern is found sequentially within
//     str
//
//   fuzzy_match(...)
//     Returns true if pattern is found AND calculates a score.
//     Performs exhaustive search via recursion to find all possible matches and
//     match with highest score. Scores values have no intrinsic meaning.
//     Possible score range is not normalized and varies with pattern. Recursion
//     is limited internally (default=10) to prevent degenerate cases
//     (pattern="aaaaaa" str="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") Uses uint8_t for
//     match indices. Therefore patterns are limited to 256 characters. Score
//     system should be tuned for YOUR use case. Words, sentences, file names,
//     or method names all prefer different tuning.

#ifndef FTS_FUZZY_MATCH_H
#define FTS_FUZZY_MATCH_H

#include <cstdint>  // uint8_t

// Public interface
namespace fts {
bool fuzzy_match_simple(char const* pattern, char const* str);
bool fuzzy_match(char const* pattern, char const* str, int& outScore);
bool fuzzy_match(char const* pattern,
                 char const* str,
                 int& outScore,
                 uint8_t* matches,
                 int maxMatches);
}  // namespace fts

#endif  // FTS_FUZZY_MATCH_H
