// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: grapheme-cluster and word segmentation over UTF-8 byte offsets.
// Why: the caret, selection, deletion and truncation must never land inside a user-perceived
//   character; every ui-text feature that moves through text uses these functions.
// Callers: TextEditor, Shaper (CaretMap, ellipsis truncation), tests.
// Preconditions: text is valid UTF-8 (sanitize first). Offsets past text.size() are clamped;
//   an offset inside a multi-byte code point is treated as the start of that code point.
//   On invalid UTF-8 the functions still terminate and never read out of bounds; the answer is
//   then unspecified but each invalid byte run counts as one U+FFFD.
// Covered (a subset of UAX #29 extended grapheme clusters, rules GB3-GB9a, GB11-GB13):
//   CR LF; controls; combining marks (Mn, Me) and spacing marks (Mc); ZWNJ and ZWJ;
//   variation selectors; emoji modifiers (skin tones); tag characters; ZWJ emoji sequences;
//   regional-indicator pairs (flags); Hangul L/V/T/LV/LVT syllable sequences.
// Not covered: Prepend characters (GB9b, e.g. Arabic number sign), Indic conjunct clusters
//   (GB9c), exact Extended_Pictographic data (an approximate range table is used, so newly
//   assigned pictographs may split), and language-specific word breaking (CJK, Thai).
#pragma once

#include <cstddef>
#include <string_view>
#include <utility>

namespace r1ui::text {

// True when `offset` is 0, text.size() or a cluster boundary.
bool isGraphemeBoundary(std::string_view text, std::size_t offset);

// Smallest boundary strictly greater than `offset`, or text.size() when none remains.
std::size_t nextGraphemeBoundary(std::string_view text, std::size_t offset);

// Largest boundary strictly smaller than `offset`, or 0 when none remains.
std::size_t prevGraphemeBoundary(std::string_view text, std::size_t offset);

// Word movement for Ctrl+Left/Right and word deletion. Words are runs of letters, digits,
// marks, underscores and emoji; punctuation/symbol runs and whitespace runs are separate units.
// nextWordBoundary lands on the start of the following word (trailing whitespace is skipped);
// prevWordBoundary lands on the start of the current or previous word.
std::size_t nextWordBoundary(std::string_view text, std::size_t offset);
std::size_t prevWordBoundary(std::string_view text, std::size_t offset);

// The unit (word, punctuation run or whitespace run) containing `offset`, as [begin, end).
// At the end of the text it is the last unit. Empty text yields {0, 0}.
std::pair<std::size_t, std::size_t> wordRangeAt(std::string_view text, std::size_t offset);

}  // namespace r1ui::text
