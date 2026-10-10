// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the derived data of BrushLibraryModel (keys, badges, alphabetical order, picture keys) shared by
//   its two implementation files; not a public header.
// Invariants: rebuilt completely by BrushLibraryModel::ensureDerived whenever the brush list or a letter
//   changes, so every vector here has exactly one element per listed brush; `alpha` is a permutation of
//   the brush indices; `alphaPos` is its inverse.
// Callers: BrushLibraryModel.cpp, BrushLibraryQuery.cpp only.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "r1ui/commands/brushes/BrushLibraryModel.h"
#include "r1ui/commands/brushes/BrushLetters.h"

namespace r1ui::commands::brushes {

struct BrushLibraryModel::Derived {
  struct Entry {
    std::u32string key;       // [override] + the name's letters, folded
    std::u32string nameFold;  // the whole name folded, for searching anywhere
    uint64_t thumbKey = 1;
    uint32_t alphaPos = 0;    // position in the alphabetical order
    uint8_t uniqueLength = 0; // key characters of the badge
    bool ambiguous = false;   // no prefix of the key is unique
    bool favourite = false;
    char32_t userLetter = 0;
    char32_t hostLetter = 0;
    std::string badge;        // upper-case
    bool hasOverride() const { return userLetter != 0 || hostLetter != 0; }
  };
  std::vector<Entry> entries;
  std::vector<uint32_t> alpha;
  std::vector<std::string> categories;
  std::unordered_map<uint64_t, uint32_t> byThumb;
};

}  // namespace r1ui::commands::brushes
