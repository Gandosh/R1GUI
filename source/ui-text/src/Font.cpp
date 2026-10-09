// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: FontLibrary loading and validation, Font metrics, and FontFamily resolution.
// Why: this is the only place untrusted font bytes become a Font; every later stage (shaping,
//   rasterization) relies on the checks done here (scalable face, sane units-per-em, glyphs).
// Failure behavior: all failures are returned as Result errors; exceptions (allocation) are
//   caught at the loader boundary. A failed load destroys everything it created.
#include "r1ui/text/Font.h"

#include <hb-ot.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>

#include "FontImpl.h"
#include "r1ui/core/CheckedCast.h"

namespace r1ui::text {

namespace {

constexpr int kMinUnitsPerEm = 16;
constexpr int kMaxUnitsPerEm = 16384;
constexpr std::size_t kMaxPathBytes = 32767;

std::uint32_t nextFontId() {
  static std::atomic<std::uint32_t> counter{1};
  return counter.fetch_add(1);
}

Error loadError(ErrorCode code, const char* what) { return makeError(code, what); }

}  // namespace

bool isValidPixelSize(float size) {
  return std::isfinite(size) && size >= kMinPixelSize && size <= kMaxPixelSize;
}

float defaultEmboldenPx(float pixelSize) {
  if (!isValidPixelSize(pixelSize)) return 0.0f;
  constexpr float kSmall = 1.0f / 24.0f;  // applies at 9 px and below
  constexpr float kLarge = 1.0f / 32.0f;  // applies at 36 px and above
  float k = kSmall;
  if (pixelSize >= 36.0f) {
    k = kLarge;
  } else if (pixelSize > 9.0f) {
    k = kSmall + (kLarge - kSmall) * ((pixelSize - 9.0f) / 27.0f);
  }
  return pixelSize * k;
}

// ---- FontLibrary ---------------------------------------------------------------------------

struct FontLibrary::Impl {
  std::shared_ptr<FtLibrary> ft;
};

FontLibrary::FontLibrary() : impl_(std::make_shared<Impl>()) {
  auto ft = std::make_shared<FtLibrary>();
  if (FT_Init_FreeType(&ft->library) == 0) impl_->ft = std::move(ft);
}

FontLibrary::~FontLibrary() = default;

bool FontLibrary::ready() const { return impl_->ft != nullptr; }

Result<FontHandle> FontLibrary::loadFromMemory(std::span<const std::uint8_t> data, int weight) {
  if (!ready()) return loadError(ErrorCode::Internal, "FreeType is not initialized");
  if (data.empty()) return loadError(ErrorCode::InvalidArgument, "font data is empty");
  if (data.size() > kMaxFontFileBytes) return loadError(ErrorCode::FileTooLarge, "font data too large");
  if (weight < 1 || weight > 1000) return loadError(ErrorCode::InvalidArgument, "weight outside 1..1000");

  try {
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(data.begin(), data.end());
    std::unique_ptr<Font::Impl> impl = std::make_unique<Font::Impl>();
    impl->ft = impl_->ft;
    impl->bytes = bytes;

    const FT_Long length = r1ui::core::checkedCast<FT_Long>(bytes->size());
    if (FT_New_Memory_Face(impl->ft->library, bytes->data(), length, 0, &impl->face) != 0) {
      return loadError(ErrorCode::CorruptFont, "not a TrueType/OpenType font");
    }
    if (!FT_IS_SCALABLE(impl->face) || impl->face->num_glyphs <= 0) {
      return loadError(ErrorCode::CorruptFont, "face has no scalable outlines");
    }
    const int upem = impl->face->units_per_EM;
    if (upem < kMinUnitsPerEm || upem > kMaxUnitsPerEm) {
      return loadError(ErrorCode::CorruptFont, "units per em outside 16..16384");
    }

    const unsigned int hbLength = r1ui::core::checkedCast<unsigned int>(bytes->size());
    impl->blob = hb_blob_create(reinterpret_cast<const char*>(bytes->data()), hbLength,
                                HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    impl->hbFace = hb_face_create(impl->blob, 0);
    if (impl->hbFace == nullptr || hb_face_get_glyph_count(impl->hbFace) == 0 ||
        static_cast<int>(hb_face_get_upem(impl->hbFace)) != upem) {
      return loadError(ErrorCode::CorruptFont, "HarfBuzz rejected the font tables");
    }
    impl->hbFont = hb_font_create(impl->hbFace);
    hb_font_set_scale(impl->hbFont, upem, upem);

    FontHandle font(new Font());
    font->id_ = nextFontId();
    font->weight_ = weight;
    font->unitsPerEm_ = upem;
    font->glyphCount_ = hb_face_get_glyph_count(impl->hbFace);
    font->impl_ = std::move(impl);
    return font;
  } catch (const std::bad_alloc&) {
    return loadError(ErrorCode::Internal, "out of memory while loading the font");
  } catch (const std::range_error&) {
    return loadError(ErrorCode::FileTooLarge, "font size does not fit the library limits");
  }
}

Result<FontHandle> FontLibrary::loadFromFile(std::string_view utf8Path, int weight) {
  if (utf8Path.empty() || utf8Path.size() > kMaxPathBytes ||
      utf8Path.find('\0') != std::string_view::npos) {
    return loadError(ErrorCode::InvalidArgument, "font path is empty, too long or contains NUL");
  }
  try {
    const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
      return loadError(ErrorCode::FileNotFound, "font path is not a regular file");
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return loadError(ErrorCode::ReadFailed, "cannot determine the font file size");
    if (size == 0) return loadError(ErrorCode::CorruptFont, "font file is empty");
    if (size > kMaxFontFileBytes) return loadError(ErrorCode::FileTooLarge, "font file too large");

    std::ifstream in(path, std::ios::binary);
    if (!in) return loadError(ErrorCode::ReadFailed, "cannot open the font file");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) {
      return loadError(ErrorCode::ReadFailed, "short read on the font file");
    }
    return loadFromMemory(data, weight);
  } catch (const std::bad_alloc&) {
    return loadError(ErrorCode::Internal, "out of memory while reading the font");
  } catch (const std::filesystem::filesystem_error&) {
    return loadError(ErrorCode::ReadFailed, "file system error while reading the font");
  }
}

// ---- Font ----------------------------------------------------------------------------------

Font::~Font() = default;

FontMetrics Font::metricsAt(float pixelSize) const {
  FontMetrics m;
  if (!isValidPixelSize(pixelSize)) return m;
  const float scale = pixelSize / static_cast<float>(unitsPerEm_);
  const auto read = [&](hb_ot_metrics_tag_t tag) {
    hb_position_t value = 0;
    hb_ot_metrics_get_position_with_fallback(impl_->hbFont, tag, &value);
    return static_cast<float>(value) * scale;
  };
  m.ascent = read(HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER);
  m.descent = -read(HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER);
  m.lineGap = read(HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP);
  m.xHeight = read(HB_OT_METRICS_TAG_X_HEIGHT);
  m.capHeight = read(HB_OT_METRICS_TAG_CAP_HEIGHT);
  m.naturalLineHeight = m.ascent + m.descent + m.lineGap;
  return m;
}

// ---- FontFamily ----------------------------------------------------------------------------

Status FontFamily::addFace(FontHandle font) {
  if (!font) return makeError(ErrorCode::InvalidArgument, "null font");
  if (font->weight() < 1 || font->weight() > 1000) {
    return makeError(ErrorCode::InvalidArgument, "weight outside 1..1000");
  }
  for (FontHandle& existing : faces_) {
    if (existing->weight() == font->weight()) {
      existing = std::move(font);
      return {};
    }
  }
  faces_.push_back(std::move(font));
  return {};
}

namespace {

// The face whose weight is closest to `weight`; ties go to the lighter face.
FontHandle closestFace(const std::vector<FontHandle>& faces, int weight) {
  FontHandle best;
  for (const FontHandle& f : faces) {
    if (!best) {
      best = f;
      continue;
    }
    const int d = std::abs(f->weight() - weight);
    const int bd = std::abs(best->weight() - weight);
    if (d < bd || (d == bd && f->weight() < best->weight())) best = f;
  }
  return best;
}

}  // namespace

ResolvedFace FontFamily::resolve(int weight, float pixelSize, BoldMode mode, float emboldenOverride) const {
  ResolvedFace out;
  if (faces_.empty() || !isValidPixelSize(pixelSize)) return out;
  if (mode == BoldMode::RealFaces) {
    out.font = closestFace(faces_, weight);
    return out;
  }
  out.font = closestFace(faces_, 400);
  if (weight >= 500) {
    const bool overridden = std::isfinite(emboldenOverride) && emboldenOverride >= 0.0f;
    out.emboldenPx = overridden ? std::min(emboldenOverride, pixelSize) : defaultEmboldenPx(pixelSize);
  }
  return out;
}

}  // namespace r1ui::text
