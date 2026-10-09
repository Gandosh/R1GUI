// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Png.h: chunk framing and CRC-32, Adler-32, stored-block deflate for the
//   encoder, and a bounded inflate (fixed and dynamic Huffman, stored blocks) with the PNG scanline
//   unfilter for the decoder.
// Invariants: the decoder computes the exact raw size from the validated IHDR first and refuses to
//   inflate more than that, so a decompression bomb costs at most that much memory; every chunk
//   length is checked against the remaining input before it is read.
// Callers: image::compare / visual harness, tests, examples/preview.
#include "r1ui/widgets/image/Png.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "r1ui/core/CheckedCast.h"

namespace r1ui::widgets::image {

namespace {

constexpr size_t kMaxStoredBlock = 65535;
constexpr size_t kMaxPngFileBytes = size_t{512} * 1024 * 1024;

const std::array<uint32_t, 256>& crcTable() {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  return table;
}

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu) {
  const auto& table = crcTable();
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return crc;
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(v >> shift));
}

void putChunk(std::vector<uint8_t>& out, const char (&type)[5], const std::vector<uint8_t>& body) {
  putU32(out, r1ui::core::checkedCast<uint32_t>(body.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), body.begin(), body.end());
  putU32(out, ~crc32(out.data() + start, out.size() - start));
}

uint32_t adler32(const std::vector<uint8_t>& data) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (uint8_t byte : data) {
    a = (a + byte) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

uint32_t getU32(const uint8_t* p) { return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3]; }

// ---- inflate ----------------------------------------------------------------------------------
// A compact RFC 1951 decoder: canonical Huffman tables decoded bit by bit (slow but tiny, and
// reference crops are a few kilobytes). `limit` caps the output size.

class Inflater {
 public:
  Inflater(std::span<const uint8_t> in, size_t limit) : in_(in), limit_(limit) {}

  // Returns an empty string on success, otherwise the reason.
  std::string run(std::vector<uint8_t>& out) {
    out_ = &out;
    if (in_.size() < 2) return "zlib stream too short";
    const uint8_t cmf = in_[0];
    const uint8_t flg = in_[1];
    if ((cmf & 0x0F) != 8 || ((uint32_t{cmf} << 8) | flg) % 31 != 0 || (flg & 0x20) != 0) return "unsupported zlib header";
    pos_ = 2;
    bool last = false;
    while (!last) {
      last = bits(1) != 0;
      const uint32_t type = bits(2);
      if (failed_) return "truncated deflate stream";
      std::string error;
      if (type == 0) error = stored();
      else if (type == 1) error = fixedBlock();
      else if (type == 2) error = dynamicBlock();
      else error = "bad deflate block type";
      if (!error.empty()) return error;
      if (failed_) return "truncated deflate stream";
    }
    return {};
  }

 private:
  struct Huffman {
    std::array<uint16_t, 16> count{};
    std::array<uint16_t, 288> symbol{};
  };

  uint32_t bits(int need) {
    uint32_t value = bitBuffer_;
    while (bitCount_ < need) {
      if (pos_ >= in_.size()) {
        failed_ = true;
        return 0;
      }
      value |= uint32_t{in_[pos_++]} << bitCount_;
      bitCount_ += 8;
    }
    bitBuffer_ = value >> need;
    bitCount_ -= need;
    return value & ((1u << need) - 1u);
  }

  // Builds canonical tables; returns false for an over-subscribed set of lengths.
  static bool build(Huffman& h, const uint8_t* lengths, size_t n) {
    h.count.fill(0);
    for (size_t i = 0; i < n; ++i) ++h.count[lengths[i]];
    int left = 1;
    for (int len = 1; len <= 15; ++len) {
      left <<= 1;
      left -= h.count[static_cast<size_t>(len)];
      if (left < 0) return false;
    }
    std::array<uint16_t, 16> offsets{};
    for (int len = 1; len < 15; ++len) offsets[static_cast<size_t>(len) + 1] = static_cast<uint16_t>(offsets[static_cast<size_t>(len)] + h.count[static_cast<size_t>(len)]);
    for (size_t i = 0; i < n; ++i) {
      if (lengths[i] != 0) h.symbol[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
    }
    return true;
  }

  int decode(const Huffman& h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (size_t len = 1; len <= 15; ++len) {
      code |= static_cast<int>(bits(1));
      if (failed_) return -1;
      const int count = h.count[len];
      if (code - count < first) return h.symbol[static_cast<size_t>(index + (code - first))];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  }

  std::string stored() {
    bitBuffer_ = 0;
    bitCount_ = 0;
    if (pos_ + 4 > in_.size()) return "truncated stored block";
    const uint32_t len = in_[pos_] | (uint32_t{in_[pos_ + 1]} << 8);
    const uint32_t nlen = in_[pos_ + 2] | (uint32_t{in_[pos_ + 3]} << 8);
    pos_ += 4;
    if ((len ^ 0xFFFFu) != nlen) return "bad stored block length";
    if (pos_ + len > in_.size()) return "truncated stored block";
    if (out_->size() + len > limit_) return "pixel data larger than the header allows";
    out_->insert(out_->end(), in_.begin() + static_cast<std::ptrdiff_t>(pos_), in_.begin() + static_cast<std::ptrdiff_t>(pos_ + len));
    pos_ += len;
    return {};
  }

  std::string codes(const Huffman& lit, const Huffman& dist) {
    static constexpr std::array<uint16_t, 29> kLenBase = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static constexpr std::array<uint8_t, 29> kLenExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static constexpr std::array<uint16_t, 30> kDistBase = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static constexpr std::array<uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
      const int symbol = decode(lit);
      if (symbol < 0) return failed_ ? "truncated deflate stream" : "bad Huffman code";
      if (symbol < 256) {
        if (out_->size() >= limit_) return "pixel data larger than the header allows";
        out_->push_back(static_cast<uint8_t>(symbol));
      } else if (symbol == 256) {
        return {};
      } else {
        const size_t li = static_cast<size_t>(symbol - 257);
        if (li >= kLenBase.size()) return "bad length symbol";
        const size_t length = kLenBase[li] + bits(kLenExtra[li]);
        const int ds = decode(dist);
        if (ds < 0 || static_cast<size_t>(ds) >= kDistBase.size()) return failed_ ? "truncated deflate stream" : "bad distance symbol";
        const size_t distance = kDistBase[static_cast<size_t>(ds)] + bits(kDistExtra[static_cast<size_t>(ds)]);
        if (failed_) return "truncated deflate stream";
        if (distance > out_->size()) return "distance reaches before the start of the data";
        if (out_->size() + length > limit_) return "pixel data larger than the header allows";
        for (size_t i = 0; i < length; ++i) out_->push_back((*out_)[out_->size() - distance]);
      }
    }
  }

  std::string fixedBlock() {
    Huffman lit;
    Huffman dist;
    std::array<uint8_t, 288> lengths{};
    for (size_t i = 0; i < 288; ++i) lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    build(lit, lengths.data(), 288);
    std::array<uint8_t, 30> distLengths{};
    distLengths.fill(5);
    build(dist, distLengths.data(), 30);
    return codes(lit, dist);
  }

  std::string dynamicBlock() {
    static constexpr std::array<uint8_t, 19> kOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const size_t nlen = bits(5) + 257;
    const size_t ndist = bits(5) + 1;
    const size_t ncode = bits(4) + 4;
    if (failed_) return "truncated deflate stream";
    if (nlen > 286 || ndist > 30) return "bad dynamic block counts";
    std::array<uint8_t, 320> lengths{};
    for (size_t i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<uint8_t>(bits(3));
    Huffman lencode;
    if (!build(lencode, lengths.data(), 19)) return "bad code length code";
    lengths.fill(0);
    size_t index = 0;
    while (index < nlen + ndist) {
      const int symbol = decode(lencode);
      if (symbol < 0) return failed_ ? "truncated deflate stream" : "bad code length symbol";
      if (symbol < 16) {
        lengths[index++] = static_cast<uint8_t>(symbol);
        continue;
      }
      uint8_t value = 0;
      size_t repeat = 0;
      if (symbol == 16) {
        if (index == 0) return "repeat with no previous length";
        value = lengths[index - 1];
        repeat = 3 + bits(2);
      } else if (symbol == 17) {
        repeat = 3 + bits(3);
      } else {
        repeat = 11 + bits(7);
      }
      if (index + repeat > nlen + ndist) return "too many code lengths";
      while (repeat-- > 0) lengths[index++] = value;
    }
    if (lengths[256] == 0) return "missing end-of-block code";
    Huffman lit;
    Huffman dist;
    if (!build(lit, lengths.data(), nlen)) return "bad literal/length code";
    if (!build(dist, lengths.data() + nlen, ndist)) return "bad distance code";
    return codes(lit, dist);
  }

  std::span<const uint8_t> in_;
  size_t limit_;
  std::vector<uint8_t>* out_ = nullptr;
  size_t pos_ = 0;
  uint32_t bitBuffer_ = 0;
  int bitCount_ = 0;
  bool failed_ = false;
};

DecodeResult fail(std::string why) { return {std::nullopt, std::move(why)}; }

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

}  // namespace

// ---- encoder ----------------------------------------------------------------------------------

std::vector<uint8_t> encodePng(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  if (width == 0 || height == 0 || width > kMaxPngSide || height > kMaxPngSide || rgba.size() != size_t{width} * height * 4) {
    throw std::invalid_argument("encodePng: bad size");
  }
  // Scanlines: filter byte 0 followed by the row's RGBA bytes.
  std::vector<uint8_t> raw;
  raw.reserve(size_t{height} * (size_t{width} * 4 + 1));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const uint8_t* row = rgba.data() + size_t{y} * width * 4;
    raw.insert(raw.end(), row, row + size_t{width} * 4);
  }
  std::vector<uint8_t> zlib{0x78, 0x01};
  for (size_t offset = 0; offset < raw.size(); offset += kMaxStoredBlock) {
    const size_t length = std::min(kMaxStoredBlock, raw.size() - offset);
    const bool last = offset + length == raw.size();
    zlib.push_back(last ? 1 : 0);
    zlib.push_back(static_cast<uint8_t>(length & 0xFF));
    zlib.push_back(static_cast<uint8_t>(length >> 8));
    zlib.push_back(static_cast<uint8_t>(~length & 0xFF));
    zlib.push_back(static_cast<uint8_t>((~length >> 8) & 0xFF));
    zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
  }
  putU32(zlib, adler32(raw));

  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> header;
  putU32(header, width);
  putU32(header, height);
  header.insert(header.end(), {8, 6, 0, 0, 0});  // 8 bit, RGBA, deflate, adaptive filter, no interlace
  putChunk(png, "IHDR", header);
  putChunk(png, "IDAT", zlib);
  putChunk(png, "IEND", {});
  return png;
}

void writePng(const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
  const std::vector<uint8_t> bytes = encodePng(width, height, rgba);
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), r1ui::core::checkedCast<std::streamsize>(bytes.size()));
  out.flush();
  if (!out) throw std::runtime_error("cannot write " + path.string());
}

// ---- decoder ----------------------------------------------------------------------------------

DecodeResult decodePng(std::span<const uint8_t> bytes) {
  static constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0) return fail("not a PNG (bad signature)");

  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t colorType = 0;
  bool haveHeader = false;
  bool sawEnd = false;
  std::vector<uint8_t> palette;       // RGB triples
  std::vector<uint8_t> paletteAlpha;  // one byte per entry that has a tRNS value
  std::vector<uint8_t> compressed;

  size_t pos = 8;
  while (pos < bytes.size() && !sawEnd) {
    if (pos + 12 > bytes.size()) return fail("truncated chunk header");
    const uint32_t length = getU32(bytes.data() + pos);
    if (length > bytes.size() - pos - 12) return fail("chunk length exceeds the file");
    const uint8_t* type = bytes.data() + pos + 4;
    const uint8_t* body = bytes.data() + pos + 8;
    if (getU32(body + length) != ~crc32(type, size_t{length} + 4)) return fail("chunk CRC mismatch");
    const std::string name(reinterpret_cast<const char*>(type), 4);
    if (name == "IHDR") {
      if (haveHeader || length != 13) return fail("bad IHDR");
      width = getU32(body);
      height = getU32(body + 4);
      colorType = body[9];
      if (width == 0 || height == 0 || width > kMaxPngSide || height > kMaxPngSide || size_t{width} * height > kMaxPngPixels) return fail("image size out of range");
      if (body[8] != 8) return fail("only 8-bit images are supported");
      if (body[10] != 0 || body[11] != 0) return fail("unknown compression or filter method");
      if (body[12] != 0) return fail("interlaced images are not supported");
      if (colorType != 0 && colorType != 2 && colorType != 3 && colorType != 4 && colorType != 6) return fail("bad colour type");
      haveHeader = true;
    } else if (!haveHeader) {
      return fail("IHDR is not the first chunk");
    } else if (name == "PLTE") {
      if (length == 0 || length % 3 != 0 || length > 768) return fail("bad PLTE");
      palette.assign(body, body + length);
    } else if (name == "tRNS") {
      if (colorType == 3) paletteAlpha.assign(body, body + std::min<size_t>(length, 256));
    } else if (name == "IDAT") {
      if (compressed.size() + length > kMaxPngFileBytes) return fail("compressed data too large");
      compressed.insert(compressed.end(), body, body + length);
    } else if (name == "IEND") {
      sawEnd = true;
    }
    pos += size_t{length} + 12;
  }
  if (!haveHeader || !sawEnd) return fail("missing IHDR or IEND");
  if (colorType == 3 && palette.empty()) return fail("palette image without PLTE");

  const size_t channels = colorType == 0 ? 1 : colorType == 2 ? 3 : colorType == 3 ? 1 : colorType == 4 ? 2 : 4;
  const size_t rowBytes = size_t{width} * channels;
  const size_t rawSize = size_t{height} * (rowBytes + 1);
  std::vector<uint8_t> raw;
  // Deflate expands at most 1032:1, so a header that claims more than the data can hold must not make us
  // reserve it before the stream is read (a 57 byte file could ask for hundreds of megabytes).
  raw.reserve(std::min(rawSize, compressed.size() * 1032 + 4096));
  const std::string inflateError = Inflater(compressed, rawSize).run(raw);
  if (!inflateError.empty()) return fail(inflateError);
  if (raw.size() != rawSize) return fail("pixel data shorter than the header requires");

  // Undo the scanline filters in place (each row against the reconstructed previous row).
  std::vector<uint8_t> pixels(size_t{height} * rowBytes);
  for (size_t y = 0; y < height; ++y) {
    const uint8_t filter = raw[y * (rowBytes + 1)];
    const uint8_t* src = raw.data() + y * (rowBytes + 1) + 1;
    uint8_t* dst = pixels.data() + y * rowBytes;
    const uint8_t* up = y > 0 ? dst - rowBytes : nullptr;
    if (filter > 4) return fail("bad scanline filter");
    for (size_t i = 0; i < rowBytes; ++i) {
      const int a = i >= channels ? dst[i - channels] : 0;
      const int b = up != nullptr ? up[i] : 0;
      const int c = (up != nullptr && i >= channels) ? up[i - channels] : 0;
      int predicted = 0;
      switch (filter) {
        case 1: predicted = a; break;
        case 2: predicted = b; break;
        case 3: predicted = (a + b) / 2; break;
        case 4: predicted = paeth(a, b, c); break;
        default: break;
      }
      dst[i] = static_cast<uint8_t>(src[i] + predicted);
    }
  }

  Image image;
  image.width = width;
  image.height = height;
  image.rgba.resize(size_t{width} * height * 4);
  for (size_t i = 0; i < size_t{width} * height; ++i) {
    const uint8_t* s = pixels.data() + i * channels;
    uint8_t* d = image.rgba.data() + i * 4;
    switch (colorType) {
      case 0: d[0] = d[1] = d[2] = s[0]; d[3] = 255; break;
      case 2: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; break;
      case 3: {
        const size_t index = s[0];
        if (index * 3 + 2 >= palette.size()) return fail("palette index out of range");
        d[0] = palette[index * 3];
        d[1] = palette[index * 3 + 1];
        d[2] = palette[index * 3 + 2];
        d[3] = index < paletteAlpha.size() ? paletteAlpha[index] : uint8_t{255};
        break;
      }
      case 4: d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; break;
      default: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; break;
    }
  }
  return {std::move(image), {}};
}

DecodeResult loadPng(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return fail("cannot read " + path.string());
  if (size > kMaxPngFileBytes) return fail("file too large: " + path.string());
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  in.read(reinterpret_cast<char*>(bytes.data()), r1ui::core::checkedCast<std::streamsize>(bytes.size()));
  if (!in) return fail("cannot read " + path.string());
  DecodeResult result = decodePng(bytes);
  if (!result.ok()) result.error = path.string() + ": " + result.error;
  return result;
}

}  // namespace r1ui::widgets::image
