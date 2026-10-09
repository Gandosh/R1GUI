// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the error vocabulary shared by every ui-text boundary (font loading, shaping,
//   rasterization, atlas), so callers handle failures as values instead of exceptions.
// Why: font files and text come from outside the process; a damaged font or an oversized
//   string must never throw through the UI loop or leave half-built state behind.
// Callers: all ui-text public APIs and their consumers (ui-render, widgets).
// Failure behavior: a Result holds either a value or an Error. Reading the wrong side is a
//   programming error and is caught by assert in debug builds.
#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <utility>

namespace r1ui::text {

enum class ErrorCode {
  InvalidArgument,  // NaN/zero/negative/out-of-range size, empty path, null data
  FileNotFound,     // path does not name a regular file
  FileTooLarge,     // font file exceeds the documented size limit
  ReadFailed,       // I/O error while reading
  CorruptFont,      // not a usable TrueType/OpenType font
  InputTooLarge,    // text longer than kMaxShapeBytes (never silently truncated)
  GlyphTooLarge,    // glyph bitmap cannot fit the atlas
  AtlasFull,        // no room after the bounded eviction passes
  Internal          // library call failed unexpectedly (allocation, FreeType, HarfBuzz)
};

struct Error {
  ErrorCode code = ErrorCode::Internal;
  std::string message;
};

// Success-or-error carrier for operations that produce a value.
template <class T>
class Result {
 public:
  Result(T value) : value_(std::move(value)) {}      // NOLINT: implicit by design
  Result(Error error) : error_(std::move(error)) {}  // NOLINT: implicit by design

  bool ok() const { return value_.has_value(); }
  T& value() {
    assert(value_.has_value());
    return *value_;
  }
  const T& value() const {
    assert(value_.has_value());
    return *value_;
  }
  const Error& error() const { return error_; }

 private:
  std::optional<T> value_;
  Error error_;
};

// Success-or-error carrier for operations with no value.
class Status {
 public:
  Status() = default;
  Status(Error error) : failed_(true), error_(std::move(error)) {}  // NOLINT: implicit by design
  bool ok() const { return !failed_; }
  const Error& error() const { return error_; }

 private:
  bool failed_ = false;
  Error error_;
};

inline Error makeError(ErrorCode code, std::string message) {
  return Error{code, std::move(message)};
}

}  // namespace r1ui::text
