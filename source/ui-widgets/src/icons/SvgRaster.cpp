// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the SVG subset parser (tags, attributes, path data, basic shapes), curve flattening and
//   the supersampled stroke/fill rasteriser declared in SvgRaster.h.
// Invariants: every loop is bounded by the input size or by kMax* limits; every number is checked
//   finite; a rejected file produces an error message and no partial icon.
// Callers: IconCache.cpp, tests/ui-widgets/icons.
#include "r1ui/widgets/icons/SvgRaster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace r1ui::widgets {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kCubicSegments = 16;
constexpr int kQuadSegments = 12;
constexpr int kEllipseSegments = 48;
constexpr int kCornerSegments = 8;

using Attributes = std::vector<std::pair<std::string, std::string>>;

struct Tag {
  std::string name;
  Attributes attrs;
};

const std::string* find(const Attributes& attrs, std::string_view key) {
  for (const auto& [k, v] : attrs) {
    if (k == key) return &v;
  }
  return nullptr;
}

// ---- Tag scanner ------------------------------------------------------------------------

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Advances `pos` to the next element start tag; false at the end of input or on malformed
// markup (error set). Comments, declarations and closing tags are skipped.
bool nextTag(std::string_view text, size_t& pos, Tag& tag, bool& end, std::string& error) {
  end = false;
  for (;;) {
    pos = text.find('<', pos);
    if (pos == std::string_view::npos) {
      end = true;
      return true;
    }
    if (text.compare(pos, 4, "<!--") == 0) {
      const size_t close = text.find("-->", pos + 4);
      if (close == std::string_view::npos) {
        error = "unterminated comment";
        return false;
      }
      pos = close + 3;
      continue;
    }
    if (pos + 1 < text.size() && (text[pos + 1] == '/' || text[pos + 1] == '?' || text[pos + 1] == '!')) {
      const size_t close = text.find('>', pos);
      if (close == std::string_view::npos) {
        error = "unterminated tag";
        return false;
      }
      pos = close + 1;
      continue;
    }
    break;
  }
  size_t i = pos + 1;
  const size_t nameStart = i;
  while (i < text.size() && !isSpace(text[i]) && text[i] != '>' && text[i] != '/') ++i;
  tag.name.assign(text.substr(nameStart, i - nameStart));
  tag.attrs.clear();
  if (tag.name.empty()) {
    error = "empty tag name";
    return false;
  }
  for (;;) {
    while (i < text.size() && isSpace(text[i])) ++i;
    if (i >= text.size()) {
      error = "unterminated tag <" + tag.name + ">";
      return false;
    }
    if (text[i] == '>' || text[i] == '/') {
      const size_t close = text.find('>', i);
      if (close == std::string_view::npos) {
        error = "unterminated tag <" + tag.name + ">";
        return false;
      }
      pos = close + 1;
      return true;
    }
    const size_t keyStart = i;
    while (i < text.size() && text[i] != '=' && !isSpace(text[i]) && text[i] != '>') ++i;
    const std::string key(text.substr(keyStart, i - keyStart));
    while (i < text.size() && isSpace(text[i])) ++i;
    if (i >= text.size() || text[i] != '=') {
      error = "attribute '" + key + "' has no value";
      return false;
    }
    ++i;
    while (i < text.size() && isSpace(text[i])) ++i;
    if (i >= text.size() || (text[i] != '"' && text[i] != '\'')) {
      error = "attribute '" + key + "' is not quoted";
      return false;
    }
    const char quote = text[i++];
    const size_t valueEnd = text.find(quote, i);
    if (valueEnd == std::string_view::npos) {
      error = "unterminated attribute '" + key + "'";
      return false;
    }
    tag.attrs.emplace_back(key, std::string(text.substr(i, valueEnd - i)));
    i = valueEnd + 1;
  }
}

// ---- Numbers ----------------------------------------------------------------------------

// Reads SVG numbers and arc flags from a string, skipping whitespace and commas.
class Scanner {
 public:
  explicit Scanner(std::string_view text) : text_(text) {}

  void skipSeparators() {
    while (i_ < text_.size() && (isSpace(text_[i_]) || text_[i_] == ',')) ++i_;
  }
  bool atEnd() {
    skipSeparators();
    return i_ >= text_.size();
  }
  char peek() {
    skipSeparators();
    return i_ < text_.size() ? text_[i_] : '\0';
  }
  void advance() { ++i_; }

  bool number(float& out) {
    skipSeparators();
    const size_t start = i_;
    size_t j = i_;
    if (j < text_.size() && (text_[j] == '+' || text_[j] == '-')) ++j;
    size_t digits = 0;
    while (j < text_.size() && text_[j] >= '0' && text_[j] <= '9') ++j, ++digits;
    if (j < text_.size() && text_[j] == '.') {
      ++j;
      while (j < text_.size() && text_[j] >= '0' && text_[j] <= '9') ++j, ++digits;
    }
    if (digits == 0) return false;
    if (j < text_.size() && (text_[j] == 'e' || text_[j] == 'E')) {
      size_t k = j + 1;
      if (k < text_.size() && (text_[k] == '+' || text_[k] == '-')) ++k;
      size_t expDigits = 0;
      while (k < text_.size() && text_[k] >= '0' && text_[k] <= '9') ++k, ++expDigits;
      if (expDigits > 0) j = k;
    }
    const std::string token(text_.substr(start, j - start));
    char* endPtr = nullptr;
    const double value = std::strtod(token.c_str(), &endPtr);
    if (endPtr == token.c_str() || !std::isfinite(value) || std::fabs(value) > 1.0e6) return false;
    out = static_cast<float>(value);
    i_ = j;
    return true;
  }

  // An arc flag is a single '0' or '1' character that may be glued to the next number.
  bool flag(bool& out) {
    skipSeparators();
    if (i_ >= text_.size() || (text_[i_] != '0' && text_[i_] != '1')) return false;
    out = text_[i_] == '1';
    ++i_;
    return true;
  }

 private:
  std::string_view text_;
  size_t i_ = 0;
};

bool parseNumberAttr(const Attributes& attrs, std::string_view key, float fallback, float& out) {
  const std::string* value = find(attrs, key);
  if (value == nullptr) {
    out = fallback;
    return true;
  }
  Scanner scanner(*value);
  return scanner.number(out) && scanner.atEnd();
}

// ---- Outline building -------------------------------------------------------------------

class Builder {
 public:
  explicit Builder(SvgIcon& icon, std::string& error) : icon_(icon), error_(error) {}

  bool fail(std::string message) {
    if (error_.empty()) error_ = std::move(message);
    return false;
  }
  bool addPoint(SvgOutline& outline, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return fail("non-finite coordinate");
    if (++points_ > kMaxSvgPoints) return fail("too many points");
    outline.points.push_back({x, y});
    return true;
  }
  // Stores the outline; a single point (a bare moveto) draws nothing and is dropped.
  bool commit(SvgOutline& outline, bool filled, bool stroked) {
    if (outline.points.size() < 2) {
      outline = SvgOutline{};
      return true;
    }
    if (icon_.outlines.size() >= kMaxSvgElements * 8) return fail("too many sub-paths");
    outline.filled = filled;
    outline.stroked = stroked;
    icon_.outlines.push_back(std::move(outline));
    outline = SvgOutline{};
    return true;
  }

 private:
  SvgIcon& icon_;
  std::string& error_;
  size_t points_ = 0;
};

bool arcTo(Builder& b, SvgOutline& out, SvgPoint from, float rx, float ry, float rotationDeg, bool large, bool sweep,
           SvgPoint to) {
  if (from.x == to.x && from.y == to.y) return true;
  rx = std::fabs(rx);
  ry = std::fabs(ry);
  if (rx == 0.0f || ry == 0.0f) return b.addPoint(out, to.x, to.y);
  const float phi = rotationDeg * kPi / 180.0f;
  const float cosPhi = std::cos(phi);
  const float sinPhi = std::sin(phi);
  const float dx = (from.x - to.x) * 0.5f;
  const float dy = (from.y - to.y) * 0.5f;
  const float x1 = cosPhi * dx + sinPhi * dy;
  const float y1 = -sinPhi * dx + cosPhi * dy;
  const float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
  if (lambda > 1.0f) {
    const float s = std::sqrt(lambda);
    rx *= s;
    ry *= s;
  }
  const float numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  const float denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  float coefficient = denominator == 0.0f ? 0.0f : std::sqrt(std::max(0.0f, numerator / denominator));
  if (large == sweep) coefficient = -coefficient;
  const float cxp = coefficient * rx * y1 / ry;
  const float cyp = -coefficient * ry * x1 / rx;
  const float cx = cosPhi * cxp - sinPhi * cyp + (from.x + to.x) * 0.5f;
  const float cy = sinPhi * cxp + cosPhi * cyp + (from.y + to.y) * 0.5f;
  const auto angle = [](float ux, float uy, float vx, float vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  const float theta1 = angle(1.0f, 0.0f, (x1 - cxp) / rx, (y1 - cyp) / ry);
  float delta = angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
  if (!sweep && delta > 0.0f) delta -= 2.0f * kPi;
  if (sweep && delta < 0.0f) delta += 2.0f * kPi;
  const int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(delta) / (kPi / 18.0f))));
  for (int i = 1; i <= steps; ++i) {
    const float t = theta1 + delta * static_cast<float>(i) / static_cast<float>(steps);
    const float px = rx * std::cos(t);
    const float py = ry * std::sin(t);
    if (!b.addPoint(out, cosPhi * px - sinPhi * py + cx, sinPhi * px + cosPhi * py + cy)) return false;
  }
  return true;
}

bool cubicTo(Builder& b, SvgOutline& out, SvgPoint p0, SvgPoint p1, SvgPoint p2, SvgPoint p3) {
  for (int i = 1; i <= kCubicSegments; ++i) {
    const float t = static_cast<float>(i) / kCubicSegments;
    const float u = 1.0f - t;
    const float x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x;
    const float y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y;
    if (!b.addPoint(out, x, y)) return false;
  }
  return true;
}

bool quadTo(Builder& b, SvgOutline& out, SvgPoint p0, SvgPoint p1, SvgPoint p2) {
  for (int i = 1; i <= kQuadSegments; ++i) {
    const float t = static_cast<float>(i) / kQuadSegments;
    const float u = 1.0f - t;
    if (!b.addPoint(out, u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y)) {
      return false;
    }
  }
  return true;
}

// Path data interpreter: absolute and relative M L H V C S Q T A Z with implicit repeats.
bool parsePath(Builder& b, std::string_view data, bool filled, bool stroked) {
  Scanner s(data);
  SvgOutline out;
  SvgPoint cur{};
  SvgPoint start{};
  SvgPoint lastControl{};
  char command = '\0';
  char previous = '\0';
  bool haveStart = false;
  const auto number = [&](float& v) { return s.number(v) || b.fail("bad number in path data"); };
  while (!s.atEnd()) {
    const char c = s.peek();
    const bool isCommand = std::string_view("MmLlHhVvCcSsQqTtAaZz").find(c) != std::string_view::npos;
    if (isCommand) {
      s.advance();
      command = c;
    } else if (command == '\0') {
      return b.fail("path data must start with a command");
    } else if (command == 'Z' || command == 'z') {
      return b.fail("number after Z");
    } else if (command == 'M') {
      command = 'L';  // implicit lineto after moveto
    } else if (command == 'm') {
      command = 'l';
    }
    const bool rel = command >= 'a';
    const char upper = static_cast<char>(rel ? command - 'a' + 'A' : command);
    const float ox = rel ? cur.x : 0.0f;
    const float oy = rel ? cur.y : 0.0f;
    float a = 0, bb = 0, cc = 0, d = 0, e = 0, f = 0;
    bool largeArc = false;
    bool sweepFlag = false;
    switch (upper) {
      case 'M': {
        if (!number(a) || !number(bb)) return false;
        if (!b.commit(out, filled, stroked)) return false;
        cur = {ox + a, oy + bb};
        start = cur;
        haveStart = true;
        if (!b.addPoint(out, cur.x, cur.y)) return false;
        break;
      }
      case 'L':
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(a) || !number(bb)) return false;
        cur = {ox + a, oy + bb};
        if (!b.addPoint(out, cur.x, cur.y)) return false;
        break;
      case 'H':
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(a)) return false;
        cur.x = ox + a;
        if (!b.addPoint(out, cur.x, cur.y)) return false;
        break;
      case 'V':
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(a)) return false;
        cur.y = oy + a;
        if (!b.addPoint(out, cur.x, cur.y)) return false;
        break;
      case 'C': {
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(a) || !number(bb) || !number(cc) || !number(d) || !number(e) || !number(f)) return false;
        const SvgPoint p1{ox + a, oy + bb};
        const SvgPoint p2{ox + cc, oy + d};
        const SvgPoint p3{ox + e, oy + f};
        if (!cubicTo(b, out, cur, p1, p2, p3)) return false;
        lastControl = p2;
        cur = p3;
        break;
      }
      case 'S': {
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(cc) || !number(d) || !number(e) || !number(f)) return false;
        const bool smooth = previous == 'C' || previous == 'S';
        const SvgPoint p1 = smooth ? SvgPoint{2 * cur.x - lastControl.x, 2 * cur.y - lastControl.y} : cur;
        const SvgPoint p2{ox + cc, oy + d};
        const SvgPoint p3{ox + e, oy + f};
        if (!cubicTo(b, out, cur, p1, p2, p3)) return false;
        lastControl = p2;
        cur = p3;
        break;
      }
      case 'Q': {
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(a) || !number(bb) || !number(cc) || !number(d)) return false;
        const SvgPoint p1{ox + a, oy + bb};
        const SvgPoint p2{ox + cc, oy + d};
        if (!quadTo(b, out, cur, p1, p2)) return false;
        lastControl = p1;
        cur = p2;
        break;
      }
      case 'T': {
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(cc) || !number(d)) return false;
        const bool smooth = previous == 'Q' || previous == 'T';
        const SvgPoint p1 = smooth ? SvgPoint{2 * cur.x - lastControl.x, 2 * cur.y - lastControl.y} : cur;
        const SvgPoint p2{ox + cc, oy + d};
        if (!quadTo(b, out, cur, p1, p2)) return false;
        lastControl = p1;
        cur = p2;
        break;
      }
      case 'A': {
        if (!haveStart) return b.fail("path draws before a moveto");
        if (!number(a) || !number(bb) || !number(cc)) return false;
        if (!s.flag(largeArc) || !s.flag(sweepFlag)) return b.fail("bad arc flag in path data");
        if (!number(e) || !number(f)) return false;
        const SvgPoint to{ox + e, oy + f};
        if (!arcTo(b, out, cur, a, bb, cc, largeArc, sweepFlag, to)) return false;
        cur = to;
        break;
      }
      case 'Z':
        if (!haveStart) return b.fail("close before a moveto");
        out.closed = true;
        if (!b.commit(out, filled, stroked)) return false;
        cur = start;
        // After a close, drawing continues from the sub-path start with a fresh outline.
        if (!b.addPoint(out, cur.x, cur.y)) return false;
        break;
      default:
        return b.fail("unknown path command");
    }
    previous = upper;
  }
  return b.commit(out, filled, stroked);
}

// Closed rounded rectangle outline (rx, ry already clamped).
bool rectOutline(Builder& b, float x, float y, float w, float h, float rx, float ry, bool filled, bool stroked) {
  SvgOutline out;
  out.closed = true;
  const auto corner = [&](float cx, float cy, float startAngle) {
    for (int i = 0; i <= kCornerSegments; ++i) {
      const float t = startAngle + (kPi / 2.0f) * static_cast<float>(i) / kCornerSegments;
      if (!b.addPoint(out, cx + rx * std::cos(t), cy + ry * std::sin(t))) return false;
    }
    return true;
  };
  if (!corner(x + w - rx, y + ry, -kPi / 2.0f) || !corner(x + w - rx, y + h - ry, 0.0f) ||
      !corner(x + rx, y + h - ry, kPi / 2.0f) || !corner(x + rx, y + ry, kPi)) {
    return false;
  }
  return b.commit(out, filled, stroked);
}

bool ellipseOutline(Builder& b, float cx, float cy, float rx, float ry, bool filled, bool stroked) {
  SvgOutline out;
  out.closed = true;
  for (int i = 0; i < kEllipseSegments; ++i) {
    const float t = 2.0f * kPi * static_cast<float>(i) / kEllipseSegments;
    if (!b.addPoint(out, cx + rx * std::cos(t), cy + ry * std::sin(t))) return false;
  }
  return b.commit(out, filled, stroked);
}

bool pointList(Builder& b, const std::string& data, bool closed, bool filled, bool stroked) {
  Scanner s(data);
  SvgOutline out;
  out.closed = closed;
  while (!s.atEnd()) {
    float x = 0;
    float y = 0;
    if (!s.number(x) || !s.number(y)) return b.fail("bad points list");
    if (!b.addPoint(out, x, y)) return false;
  }
  if (out.points.size() < 2) return b.fail("a polyline needs at least two points");
  return b.commit(out, filled, stroked);
}

// Reads the shape element `tag` into outlines.
bool addElement(Builder& b, const Tag& tag, size_t& elements) {
  if (++elements > kMaxSvgElements) return b.fail("too many elements");
  const std::string* fillAttr = find(tag.attrs, "fill");
  const std::string* strokeAttr = find(tag.attrs, "stroke");
  const bool filled = fillAttr != nullptr && *fillAttr == "currentColor";
  const bool stroked = strokeAttr == nullptr || *strokeAttr != "none";
  const Attributes& a = tag.attrs;
  float x = 0, y = 0, w = 0, h = 0, r = 0, rx = 0, ry = 0, x2 = 0, y2 = 0;
  if (tag.name == "path") {
    const std::string* d = find(a, "d");
    if (d == nullptr) return b.fail("<path> without d");
    return parsePath(b, *d, filled, stroked);
  }
  if (tag.name == "circle") {
    if (!parseNumberAttr(a, "cx", 0, x) || !parseNumberAttr(a, "cy", 0, y) || !parseNumberAttr(a, "r", -1, r) || r < 0) {
      return b.fail("bad <circle>");
    }
    return ellipseOutline(b, x, y, r, r, filled, stroked);
  }
  if (tag.name == "ellipse") {
    if (!parseNumberAttr(a, "cx", 0, x) || !parseNumberAttr(a, "cy", 0, y) || !parseNumberAttr(a, "rx", -1, rx) ||
        !parseNumberAttr(a, "ry", -1, ry) || rx < 0 || ry < 0) {
      return b.fail("bad <ellipse>");
    }
    return ellipseOutline(b, x, y, rx, ry, filled, stroked);
  }
  if (tag.name == "rect") {
    if (!parseNumberAttr(a, "x", 0, x) || !parseNumberAttr(a, "y", 0, y) || !parseNumberAttr(a, "width", -1, w) ||
        !parseNumberAttr(a, "height", -1, h) || w < 0 || h < 0) {
      return b.fail("bad <rect>");
    }
    const bool hasRx = find(a, "rx") != nullptr;
    const bool hasRy = find(a, "ry") != nullptr;
    if (!parseNumberAttr(a, "rx", 0, rx) || !parseNumberAttr(a, "ry", 0, ry) || rx < 0 || ry < 0) return b.fail("bad <rect> radius");
    if (hasRx && !hasRy) ry = rx;
    if (hasRy && !hasRx) rx = ry;
    rx = std::min(rx, w / 2.0f);
    ry = std::min(ry, h / 2.0f);
    return rectOutline(b, x, y, w, h, rx, ry, filled, stroked);
  }
  if (tag.name == "line") {
    if (!parseNumberAttr(a, "x1", 0, x) || !parseNumberAttr(a, "y1", 0, y) || !parseNumberAttr(a, "x2", 0, x2) ||
        !parseNumberAttr(a, "y2", 0, y2)) {
      return b.fail("bad <line>");
    }
    SvgOutline out;
    return b.addPoint(out, x, y) && b.addPoint(out, x2, y2) && b.commit(out, false, stroked);
  }
  if (tag.name == "polyline" || tag.name == "polygon") {
    const std::string* pts = find(a, "points");
    if (pts == nullptr) return b.fail("<" + tag.name + "> without points");
    return pointList(b, *pts, tag.name == "polygon", filled, stroked);
  }
  return b.fail("unsupported element <" + tag.name + ">");
}

// ---- Rasteriser -------------------------------------------------------------------------

struct Segment {
  float ax, ay, bx, by;
};

float distanceSquared(const Segment& s, float px, float py) {
  const float dx = s.bx - s.ax;
  const float dy = s.by - s.ay;
  const float lengthSquared = dx * dx + dy * dy;
  float t = lengthSquared > 0.0f ? ((px - s.ax) * dx + (py - s.ay) * dy) / lengthSquared : 0.0f;
  t = std::clamp(t, 0.0f, 1.0f);
  const float cx = s.ax + t * dx - px;
  const float cy = s.ay + t * dy - py;
  return cx * cx + cy * cy;
}

// Even-odd containment over all filled outlines.
bool insideFilled(const std::vector<const SvgOutline*>& shapes, float px, float py) {
  bool inside = false;
  for (const SvgOutline* shape : shapes) {
    const auto& pts = shape->points;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
      const bool crosses = (pts[i].y > py) != (pts[j].y > py);
      if (crosses && px < (pts[j].x - pts[i].x) * (py - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x) inside = !inside;
    }
  }
  return inside;
}

}  // namespace

SvgParseResult parseSvg(std::string_view text) {
  SvgParseResult result;
  if (text.size() > kMaxSvgBytes) {
    result.error = "file is larger than the supported maximum";
    return result;
  }
  Builder builder(result.icon, result.error);
  size_t pos = 0;
  size_t elements = 0;
  bool sawSvg = false;
  Tag tag;
  for (;;) {
    bool end = false;
    if (!nextTag(text, pos, tag, end, result.error)) return result;
    if (end) break;
    if (tag.name == "svg") {
      if (sawSvg) {
        result.error = "nested <svg>";
        return result;
      }
      sawSvg = true;
      if (const std::string* vb = find(tag.attrs, "viewBox")) {
        Scanner s(*vb);
        float minX = 0, minY = 0, w = 0, h = 0;
        if (!s.number(minX) || !s.number(minY) || !s.number(w) || !s.number(h) || w <= 0 || w != h) {
          result.error = "viewBox must be a positive square";
          return result;
        }
        result.icon.viewBox = w;
      }
      float strokeWidth = 2.0f;
      if (!parseNumberAttr(tag.attrs, "stroke-width", 2.0f, strokeWidth) || strokeWidth < 0 || strokeWidth > result.icon.viewBox) {
        result.error = "bad stroke-width";
        return result;
      }
      result.icon.strokeWidth = strokeWidth;
      continue;
    }
    if (!sawSvg) {
      result.error = "content before <svg>";
      return result;
    }
    if (!addElement(builder, tag, elements)) return result;
  }
  if (!sawSvg) {
    result.error = "no <svg> element";
    return result;
  }
  if (result.icon.outlines.empty()) {
    result.error = "the icon draws nothing";
    return result;
  }
  result.ok = true;
  return result;
}

// Sample offsets inside a pixel (0..1). Smooth: a 4x4 grid (17 coverage levels). Msaa4: the four
// positions of the standard 4x multisample pattern (D3D) that the reference browser's GPU raster
// uses, so coverage matches its renders to within the geometry difference (5 levels).
std::vector<std::pair<float, float>> samplePattern(AntiAlias aa) {
  std::vector<std::pair<float, float>> points;
  if (aa == AntiAlias::Msaa4) {
    points = {{0.5f - 2.0f / 16.0f, 0.5f - 6.0f / 16.0f}, {0.5f + 6.0f / 16.0f, 0.5f - 2.0f / 16.0f},
              {0.5f - 6.0f / 16.0f, 0.5f + 2.0f / 16.0f}, {0.5f + 2.0f / 16.0f, 0.5f + 6.0f / 16.0f}};
  } else {
    for (int sy = 0; sy < 4; ++sy) {
      for (int sx = 0; sx < 4; ++sx) points.emplace_back((static_cast<float>(sx) + 0.5f) / 4.0f, (static_cast<float>(sy) + 0.5f) / 4.0f);
    }
  }
  return points;
}

std::vector<uint8_t> rasterizeIcon(const SvgIcon& icon, int size, AntiAlias aa) {
  if (size < kMinIconPixels || size > kMaxIconPixels) throw std::invalid_argument("rasterizeIcon: size out of range");
  std::vector<Segment> segments;
  std::vector<const SvgOutline*> filled;
  for (const SvgOutline& o : icon.outlines) {
    if (o.filled && o.points.size() >= 3) filled.push_back(&o);
    if (!o.stroked) continue;
    const auto& p = o.points;
    if (p.size() == 1) segments.push_back({p[0].x, p[0].y, p[0].x, p[0].y});
    for (size_t i = 1; i < p.size(); ++i) segments.push_back({p[i - 1].x, p[i - 1].y, p[i].x, p[i].y});
    if (o.closed && p.size() > 2) segments.push_back({p.back().x, p.back().y, p[0].x, p[0].y});
  }
  const float unit = icon.viewBox / static_cast<float>(size);  // viewBox units per pixel
  const float half = icon.strokeWidth * 0.5f;
  const float halfSquared = half * half;
  const float margin = unit * 0.75f;  // a pixel's farthest corner from its centre is ~0.71 px
  const std::vector<std::pair<float, float>> samples = samplePattern(aa);
  std::vector<uint8_t> coverage(static_cast<size_t>(size) * static_cast<size_t>(size), 0);
  for (int py = 0; py < size; ++py) {
    for (int px = 0; px < size; ++px) {
      const float cx = (static_cast<float>(px) + 0.5f) * unit;
      const float cy = (static_cast<float>(py) + 0.5f) * unit;
      float nearest = std::numeric_limits<float>::max();
      for (const Segment& s : segments) nearest = std::min(nearest, distanceSquared(s, cx, cy));
      const float d = std::sqrt(nearest);
      const bool maybeFilled = !filled.empty();
      if (!maybeFilled && d > half + margin) continue;
      if (!maybeFilled && d < half - margin) {
        coverage[static_cast<size_t>(py) * static_cast<size_t>(size) + static_cast<size_t>(px)] = 255;
        continue;
      }
      int hits = 0;
      for (const auto& [ox, oy] : samples) {
        const float x = (static_cast<float>(px) + ox) * unit;
        const float y = (static_cast<float>(py) + oy) * unit;
        bool covered = maybeFilled && insideFilled(filled, x, y);
        if (!covered && d <= half + margin) {
          for (const Segment& s : segments) {
            if (distanceSquared(s, x, y) <= halfSquared) {
              covered = true;
              break;
            }
          }
        }
        if (covered) ++hits;
      }
      coverage[static_cast<size_t>(py) * static_cast<size_t>(size) + static_cast<size_t>(px)] =
          static_cast<uint8_t>((hits * 255 + static_cast<int>(samples.size()) / 2) / static_cast<int>(samples.size()));
    }
  }
  return coverage;
}

}  // namespace r1ui::widgets
