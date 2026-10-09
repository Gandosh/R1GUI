// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: FlexRun, the flexbox algorithm for one container: item sizing, container sizing, line
//   breaking, flexible-length resolution, cross-axis alignment, main-axis distribution,
//   direction mirroring and absolutely positioned children.
// Why: see FlexLayout.h for the policy; this file is the CSS Flexible Box Layout resolution
//   written against our sanitised Style. Axis arithmetic is done on physical axes (0 = x /
//   width, 1 = y / height) with M_ naming the main axis and X_ the cross axis, so row and
//   column share one code path.
// Callers: Engine::measure (commit=false: only the container size) and Engine::commit
//   (commit=true: also positions and commits every child). Calls: Engine::measure/commit.
// Invariants: all numbers in play are finite and within +-kMaxExtent (sanitised styles, clamped
//   measure results); percentages resolve to NaN when their base is indefinite and NaN means
//   "auto" everywhere below.
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "LayoutEngine.h"

namespace r1ui::core::layout::detail {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double clampMinWins(double v, double lo, double hi) { return std::max(lo, std::min(v, hi)); }
double clampExtent(double v) { return std::clamp(v, -kMaxExtent, kMaxExtent); }

// px -> value, percent -> share of `base` (NaN when the base is indefinite), anything else NaN.
double resolveLen(const Length& l, double base) {
  switch (l.kind) {
    case Length::Kind::Px: return l.value;
    case Length::Kind::Percent: return std::isnan(base) ? kNaN : base * l.value / 100.0;
    default: return kNaN;
  }
}

struct Item {
  tree::WidgetId id;
  Style s;
  Align align = Align::Stretch;
  // Per physical axis (0 = x, 1 = y).
  double mStart[2] = {0, 0};
  double mEnd[2] = {0, 0};
  bool autoStart[2] = {false, false};
  bool autoEnd[2] = {false, false};
  bool fit[2] = {false, false};
  double pad[2] = {0, 0};
  double minSz[2] = {0, 0};
  double maxSz[2] = {kMaxExtent, kMaxExtent};
  double prop[2] = {kNaN, kNaN};  // resolved width/height; NaN = auto
  // Main-axis sizing.
  double base = 0;
  double hypo = 0;
  double target = 0;
  bool frozen = false;
  // Cross axis and placement (relative to the container content box).
  double hypoCross = 0;
  double cross = 0;
  double posMain = 0;
  double posCross = 0;
};

struct Line {
  size_t begin = 0;
  size_t end = 0;
  double cross = 0;
  double offset = 0;
};

class FlexRun {
 public:
  FlexRun(Engine& engine, tree::WidgetId id, const SizeConstraint& c, bool commit)
      : eng_(engine), id_(id), s_(sanitizeStyle(engine.tree().get(id)->style)), commit_(commit) {
    const Style& style = s_;
    row_ = style.direction == FlexDirection::Row || style.direction == FlexDirection::RowReverse;
    mainRev_ = style.direction == FlexDirection::RowReverse ||
               style.direction == FlexDirection::ColumnReverse;
    wrap_ = style.wrap != FlexWrap::NoWrap;
    crossRev_ = style.wrap == FlexWrap::WrapReverse;
    M_ = row_ ? 0 : 1;
    X_ = 1 - M_;
    ax_[0] = c.w;
    ax_[1] = c.h;
    for (int a = 0; a < 2; ++a) {
      padStart_[a] = style.padding[a == 0 ? kLeft : kTop];
      pad_[a] = style.padding[a == 0 ? kLeft : kTop] + style.padding[a == 0 ? kRight : kBottom];
      minEff_[a] = std::max(ax_[a].min, pad_[a]);
      maxEff_[a] = std::max(ax_[a].max, minEff_[a]);
      def_[a] = ax_[a].mode == MeasureMode::Exactly;
      outer_[a] = def_[a] ? ax_[a].size : 0.0;
      inner_[a] = def_[a] ? std::max(0.0, outer_[a] - pad_[a]) : kNaN;
    }
    gapM_ = row_ ? style.gapColumn : style.gapRow;
    gapX_ = row_ ? style.gapRow : style.gapColumn;
    availX_ = def_[X_] ? inner_[X_]
                       : (ax_[X_].mode == MeasureMode::AtMost ? std::max(0.0, ax_[X_].size - pad_[X_])
                                                              : kNaN);
  }

  Size run() {
    gather();
    if (!def_[M_]) sizeMainFromContent();
    resolveAll();
    buildLines();
    for (const Line& ln : lines_) resolveFlex(ln);
    sizeCross();
    if (commit_) placeAndCommit();
    return Size{outer_[0], outer_[1]};
  }

 private:
  // ---- collection ----
  void gather() {
    tree::WidgetTree& t = eng_.tree();
    for (tree::WidgetId c = t.firstChild(id_); c.valid(); c = t.nextSibling(c)) {
      const tree::Widget* w = t.get(c);
      if (w->style.display == Display::None) {
        hidden_.push_back(c);
        continue;
      }
      Style cs = sanitizeStyle(w->style);
      if (cs.position == Position::Absolute) {
        absolute_.push_back(c);
        continue;
      }
      Item it;
      it.id = c;
      it.s = cs;
      it.align = cs.alignSelf == Align::Auto ? s_.alignItems : cs.alignSelf;
      if (it.align == Align::Auto) it.align = Align::Stretch;
      if (it.align == Align::Baseline) it.align = Align::Start;
      for (int a = 0; a < 2; ++a) {
        const Length& ms = cs.margin[a == 0 ? kLeft : kTop];
        const Length& me = cs.margin[a == 0 ? kRight : kBottom];
        it.autoStart[a] = ms.kind == Length::Kind::Auto;
        it.autoEnd[a] = me.kind == Length::Kind::Auto;
        it.mStart[a] = it.autoStart[a] ? 0.0 : ms.value;
        it.mEnd[a] = it.autoEnd[a] ? 0.0 : me.value;
        it.pad[a] = cs.padding[a == 0 ? kLeft : kTop] + cs.padding[a == 0 ? kRight : kBottom];
        it.fit[a] = (a == 0 ? cs.width : cs.height).kind == Length::Kind::FitContent;
      }
      flow_.push_back(std::move(it));
    }
  }

  bool stretchOk(const Item& it) const {
    return it.align == Align::Stretch && !it.autoStart[X_] && !it.autoEnd[X_] && !it.fit[X_] &&
           it.s.aspectRatio <= 0.0 && std::isnan(it.prop[X_]);
  }

  static AxisConstraint axisOf(const Item& it, int a, MeasureMode mode, double size) {
    return AxisConstraint{mode, size, it.minSz[a], it.maxSz[a]};
  }

  // The cross-axis constraint under which an item is measured or laid out.
  AxisConstraint crossAxis(const Item& it) const {
    const int x = X_;
    const double mg = it.mStart[x] + it.mEnd[x];
    if (!std::isnan(it.prop[x])) {
      return axisOf(it, x, MeasureMode::Exactly, clampMinWins(it.prop[x], it.minSz[x], it.maxSz[x]));
    }
    if (stretchOk(it) && !wrap_ && !std::isnan(inner_[x])) {
      return axisOf(it, x, MeasureMode::Exactly,
                    clampMinWins(std::max(0.0, inner_[x] - mg), it.minSz[x], it.maxSz[x]));
    }
    if (!std::isnan(availX_)) return axisOf(it, x, MeasureMode::AtMost, std::max(0.0, availX_ - mg));
    return axisOf(it, x, MeasureMode::Undefined, 0.0);
  }

  Size measureItem(const Item& it, const AxisConstraint& mainAxis, const AxisConstraint& crossAx) {
    SizeConstraint sc;
    (M_ == 0 ? sc.w : sc.h) = mainAxis;
    (X_ == 0 ? sc.w : sc.h) = crossAx;
    return eng_.measure(it.id, sc);
  }

  // ---- item sizing ----
  // Resolves every item's size properties against the current inner sizes (NaN = indefinite)
  // and derives its flex base size and clamped hypothetical main size.
  void resolveAll() {
    for (Item& it : flow_) {
      for (int a = 0; a < 2; ++a) {
        const Length& sz = a == 0 ? it.s.width : it.s.height;
        const Length& mn = a == 0 ? it.s.minWidth : it.s.minHeight;
        const Length& mx = a == 0 ? it.s.maxWidth : it.s.maxHeight;
        it.prop[a] = resolveLen(sz, inner_[a]);
        const double lo = resolveLen(mn, inner_[a]);
        it.minSz[a] = std::max(std::isnan(lo) ? 0.0 : lo, it.pad[a]);
        const double hi = resolveLen(mx, inner_[a]);
        it.maxSz[a] = std::max(std::isnan(hi) ? kMaxExtent : hi, it.minSz[a]);
      }
      if (it.s.aspectRatio > 0.0) {
        if (std::isnan(it.prop[0]) && !std::isnan(it.prop[1])) it.prop[0] = it.prop[1] * it.s.aspectRatio;
        else if (std::isnan(it.prop[1]) && !std::isnan(it.prop[0])) it.prop[1] = it.prop[0] / it.s.aspectRatio;
      }
      double base;
      const Length& fb = it.s.flexBasis;
      if (fb.kind == Length::Kind::Px) base = fb.value;
      else if (fb.kind == Length::Kind::Percent && !std::isnan(inner_[M_])) base = inner_[M_] * fb.value / 100.0;
      else base = it.prop[M_];
      if (std::isnan(base)) {
        const Size r = measureItem(it, axisOf(it, M_, MeasureMode::Undefined, 0.0), crossAxis(it));
        base = M_ == 0 ? r.w : r.h;
      }
      it.base = clampExtent(std::max(base, it.pad[M_]));
      it.hypo = clampMinWins(it.base, it.minSz[M_], it.maxSz[M_]);
    }
  }

  double outerMain(const Item& it, double size) const { return size + it.mStart[M_] + it.mEnd[M_]; }

  // A content-sized main axis becomes the sum of the hypothetical sizes (max-content), capped by
  // the available space for AtMost and clamped by min/max. Percent main sizes are auto here.
  void sizeMainFromContent() {
    resolveAll();
    double total = 0.0;
    for (const Item& it : flow_) total += outerMain(it, it.hypo);
    if (!flow_.empty()) total += gapM_ * static_cast<double>(flow_.size() - 1);
    double v = total + pad_[M_];
    if (ax_[M_].mode == MeasureMode::AtMost) v = std::min(v, ax_[M_].size);
    outer_[M_] = clampMinWins(v, minEff_[M_], maxEff_[M_]);
    def_[M_] = true;
    inner_[M_] = std::max(0.0, outer_[M_] - pad_[M_]);
  }

  // ---- lines ----
  void buildLines() {
    const size_t n = flow_.size();
    if (n == 0) return;
    if (!wrap_) {
      lines_.push_back(Line{0, n, 0.0, 0.0});
      return;
    }
    size_t begin = 0;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const double outer = outerMain(flow_[i], flow_[i].hypo);
      if (i > begin && sum + gapM_ + outer > inner_[M_] + 1e-9) {
        lines_.push_back(Line{begin, i, 0.0, 0.0});
        begin = i;
        sum = outer;
      } else {
        sum += (i > begin ? gapM_ : 0.0) + outer;
      }
    }
    lines_.push_back(Line{begin, n, 0.0, 0.0});
  }

  // CSS "resolve flexible lengths": freeze inflexible items, then repeatedly distribute the
  // remaining free space over the unfrozen ones and freeze those whose clamp was violated.
  void resolveFlex(const Line& ln) {
    const size_t n = ln.end - ln.begin;
    const double gaps = gapM_ * static_cast<double>(n - 1);
    double hypoSum = gaps;
    for (size_t i = ln.begin; i < ln.end; ++i) hypoSum += outerMain(flow_[i], flow_[i].hypo);
    const bool grow = hypoSum < inner_[M_];

    double initialFree = inner_[M_] - gaps;
    for (size_t i = ln.begin; i < ln.end; ++i) {
      Item& it = flow_[i];
      const double factor = grow ? it.s.flexGrow : it.s.flexShrink;
      it.frozen = factor == 0.0 || (grow ? it.base > it.hypo : it.base < it.hypo);
      it.target = it.frozen ? it.hypo : it.base;
      initialFree -= outerMain(it, it.target);
    }

    for (size_t guard = 0; guard <= n; ++guard) {
      double used = gaps;
      double sumFactors = 0.0;
      double sumScaled = 0.0;
      size_t unfrozen = 0;
      for (size_t i = ln.begin; i < ln.end; ++i) {
        const Item& it = flow_[i];
        used += outerMain(it, it.frozen ? it.target : it.base);
        if (!it.frozen) {
          ++unfrozen;
          sumFactors += grow ? it.s.flexGrow : it.s.flexShrink;
          sumScaled += it.s.flexShrink * std::max(0.0, it.base - it.pad[M_]);
        }
      }
      if (unfrozen == 0) break;
      double remaining = inner_[M_] - used;
      if (sumFactors < 1.0) {
        const double scaled = initialFree * sumFactors;
        if (std::fabs(scaled) < std::fabs(remaining)) remaining = scaled;
      }
      double violation = 0.0;
      for (size_t i = ln.begin; i < ln.end; ++i) {
        Item& it = flow_[i];
        if (it.frozen) continue;
        double t = it.base;
        if (remaining != 0.0) {
          if (grow) {
            if (sumFactors > 0.0) t = it.base + remaining * it.s.flexGrow / sumFactors;
          } else if (sumScaled > 0.0) {
            t = it.base + remaining * (it.s.flexShrink * std::max(0.0, it.base - it.pad[M_])) / sumScaled;
          }
        }
        it.target = clampExtent(t);
      }
      std::vector<double> clamped;  // violations are decided on the unclamped targets
      clamped.reserve(unfrozen);
      for (size_t i = ln.begin; i < ln.end; ++i) {
        const Item& it = flow_[i];
        if (it.frozen) continue;
        const double c = clampMinWins(it.target, it.minSz[M_], it.maxSz[M_]);
        clamped.push_back(c);
        violation += c - it.target;
      }
      size_t k = 0;
      for (size_t i = ln.begin; i < ln.end; ++i) {
        Item& it = flow_[i];
        if (it.frozen) continue;
        const double c = clamped[k++];
        const double v = c - it.target;
        if (violation == 0.0 || (violation > 0.0 ? v > 0.0 : v < 0.0)) {
          it.target = c;
          it.frozen = true;
        }
      }
    }
    for (size_t i = ln.begin; i < ln.end; ++i) {  // the guard bound is never reached, but be safe
      Item& it = flow_[i];
      it.target = clampMinWins(it.target, it.minSz[M_], it.maxSz[M_]);
    }
  }

  // ---- cross axis ----
  void sizeCross() {
    for (Line& ln : lines_) {
      double lineCross = 0.0;
      for (size_t i = ln.begin; i < ln.end; ++i) {
        Item& it = flow_[i];
        const int x = X_;
        if (!std::isnan(it.prop[x])) {
          it.hypoCross = clampMinWins(it.prop[x], it.minSz[x], it.maxSz[x]);
        } else if (it.s.aspectRatio > 0.0) {
          const double v = M_ == 0 ? it.target / it.s.aspectRatio : it.target * it.s.aspectRatio;
          it.hypoCross = clampMinWins(clampExtent(v), it.minSz[x], it.maxSz[x]);
        } else if (stretchOk(it) && !wrap_ && def_[x]) {
          it.hypoCross = 0.0;  // will fill the line
        } else {
          const Size r = measureItem(it, axisOf(it, M_, MeasureMode::Exactly, it.target), crossAxis(it));
          it.hypoCross = clampMinWins(x == 0 ? r.w : r.h, it.minSz[x], it.maxSz[x]);
        }
        lineCross = std::max(lineCross, it.hypoCross + it.mStart[x] + it.mEnd[x]);
      }
      ln.cross = lineCross;
    }
    if (!def_[X_]) {
      double total = 0.0;
      for (const Line& ln : lines_) total += ln.cross;
      if (!lines_.empty()) total += gapX_ * static_cast<double>(lines_.size() - 1);
      double v = total + pad_[X_];
      if (ax_[X_].mode == MeasureMode::AtMost) v = std::min(v, ax_[X_].size);
      outer_[X_] = clampMinWins(v, minEff_[X_], maxEff_[X_]);
      def_[X_] = true;
      inner_[X_] = std::max(0.0, outer_[X_] - pad_[X_]);
    }
    if (!wrap_ && !lines_.empty()) lines_[0].cross = inner_[X_];  // a single line fills the container
  }

  // ---- placement ----
  static double alignOffset(Align a, double free) {
    switch (a) {
      case Align::Center: return free / 2.0;
      case Align::End: return free;
      default: return 0.0;
    }
  }

  // Offset of a lone box inside free space along the main axis (used for static positions).
  double justifyOffset(double free) const {
    double off = 0.0;
    switch (s_.justifyContent) {
      case Justify::Center:
      case Justify::SpaceAround:
      case Justify::SpaceEvenly: off = free / 2.0; break;
      case Justify::End: off = free; break;
      default: break;
    }
    return mainRev_ ? free - off : off;
  }

  void distributeLines() {
    const size_t count = lines_.size();
    if (count == 0) return;
    double total = gapX_ * static_cast<double>(count - 1);
    for (const Line& ln : lines_) total += ln.cross;
    const double free = inner_[X_] - total;
    double lead = 0.0;
    double between = 0.0;
    if (wrap_) {
      const double n = static_cast<double>(count);
      switch (s_.alignContent) {
        case AlignContent::Stretch:
          if (free > 0.0) {
            for (Line& ln : lines_) ln.cross += free / n;
          }
          break;
        case AlignContent::Center: lead = free / 2.0; break;
        case AlignContent::End: lead = free; break;
        case AlignContent::SpaceBetween:
          if (free > 0.0 && count > 1) between = free / (n - 1.0);
          break;
        case AlignContent::SpaceAround:
          if (free > 0.0) {
            between = free / n;
            lead = between / 2.0;
          }
          break;
        case AlignContent::SpaceEvenly:
          if (free > 0.0) {
            between = free / (n + 1.0);
            lead = between;
          }
          break;
        case AlignContent::Start: break;
      }
    }
    double cursor = lead;
    for (Line& ln : lines_) {
      ln.offset = cursor;
      cursor += ln.cross + gapX_ + between;
    }
  }

  void placeLine(const Line& ln) {
    const size_t n = ln.end - ln.begin;
    // Main axis: auto margins take positive free space, otherwise justify-content does.
    double used = gapM_ * static_cast<double>(n - 1);
    size_t autos = 0;
    for (size_t i = ln.begin; i < ln.end; ++i) {
      const Item& it = flow_[i];
      used += outerMain(it, it.target);
      autos += (it.autoStart[M_] ? 1u : 0u) + (it.autoEnd[M_] ? 1u : 0u);
    }
    const double free = inner_[M_] - used;
    double lead = 0.0;
    double between = 0.0;
    double autoEach = 0.0;
    if (autos > 0 && free > 0.0) {
      autoEach = free / static_cast<double>(autos);
    } else {
      const double cnt = static_cast<double>(n);
      switch (s_.justifyContent) {
        case Justify::Center: lead = free / 2.0; break;
        case Justify::End: lead = free; break;
        case Justify::SpaceBetween:
          if (free > 0.0 && n > 1) between = free / (cnt - 1.0);
          break;
        case Justify::SpaceAround:
          if (free > 0.0) {
            between = free / cnt;
            lead = between / 2.0;
          }
          break;
        case Justify::SpaceEvenly:
          if (free > 0.0) {
            between = free / (cnt + 1.0);
            lead = between;
          }
          break;
        case Justify::Start: break;
      }
    }
    double cursor = lead;
    for (size_t i = ln.begin; i < ln.end; ++i) {
      Item& it = flow_[i];
      const double ms = it.autoStart[M_] ? autoEach : it.mStart[M_];
      const double me = it.autoEnd[M_] ? autoEach : it.mEnd[M_];
      it.posMain = cursor + ms;
      cursor += ms + it.target + me + gapM_ + between;
    }
    // Cross axis inside the line.
    const int x = X_;
    for (size_t i = ln.begin; i < ln.end; ++i) {
      Item& it = flow_[i];
      double size = it.hypoCross;
      if (stretchOk(it)) {
        size = clampMinWins(std::max(0.0, ln.cross - it.mStart[x] - it.mEnd[x]), it.minSz[x], it.maxSz[x]);
      }
      it.cross = size;
      const double room = ln.cross - size - it.mStart[x] - it.mEnd[x];
      const size_t autoCount = (it.autoStart[x] ? 1u : 0u) + (it.autoEnd[x] ? 1u : 0u);
      double ms = it.mStart[x];
      double extra = 0.0;
      if (autoCount > 0 && room > 0.0) {
        const double each = room / static_cast<double>(autoCount);
        if (it.autoStart[x]) ms = each;
      } else if (autoCount == 0) {
        extra = alignOffset(it.align, room);
      }
      it.posCross = ln.offset + ms + extra;
    }
  }

  void commitChild(Item& it) {
    double pm = it.posMain;
    double pc = it.posCross;
    if (mainRev_) pm = inner_[M_] - pm - it.target;
    if (crossRev_) pc = inner_[X_] - pc - it.cross;
    const double x = padStart_[0] + (M_ == 0 ? pm : pc);
    const double y = padStart_[1] + (M_ == 1 ? pm : pc);
    const double w = M_ == 0 ? it.target : it.cross;
    const double h = M_ == 0 ? it.cross : it.target;
    if (tree::Widget* child = eng_.tree().get(it.id)) {
      child->exact.x = clampExtent(x);
      child->exact.y = clampExtent(y);
    }
    eng_.commit(it.id, w, h);
  }

  void placeAbsolute(tree::WidgetId aid) {
    tree::Widget* w = eng_.tree().get(aid);
    const Style as = sanitizeStyle(w->style);
    const double cb[2] = {outer_[0], outer_[1]};
    double pos[2];  // resolved start insets
    double endInset[2];
    double bsz[2];
    double lo[2];
    double hi[2];
    double mS[2];
    double mE[2];
    for (int a = 0; a < 2; ++a) {
      pos[a] = resolveLen(as.inset[a == 0 ? kLeft : kTop], cb[a]);
      endInset[a] = resolveLen(as.inset[a == 0 ? kRight : kBottom], cb[a]);
      const Length& ms = as.margin[a == 0 ? kLeft : kTop];
      const Length& me = as.margin[a == 0 ? kRight : kBottom];
      mS[a] = ms.kind == Length::Kind::Px ? ms.value : 0.0;
      mE[a] = me.kind == Length::Kind::Px ? me.value : 0.0;
      const double padA = as.padding[a == 0 ? kLeft : kTop] + as.padding[a == 0 ? kRight : kBottom];
      const double mn = resolveLen(a == 0 ? as.minWidth : as.minHeight, cb[a]);
      lo[a] = std::max(std::isnan(mn) ? 0.0 : mn, padA);
      const double mx = resolveLen(a == 0 ? as.maxWidth : as.maxHeight, cb[a]);
      hi[a] = std::max(std::isnan(mx) ? kMaxExtent : mx, lo[a]);
      bsz[a] = resolveLen(a == 0 ? as.width : as.height, cb[a]);
    }
    if (as.aspectRatio > 0.0) {
      if (std::isnan(bsz[0]) && !std::isnan(bsz[1])) bsz[0] = bsz[1] * as.aspectRatio;
      else if (std::isnan(bsz[1]) && !std::isnan(bsz[0])) bsz[1] = bsz[0] / as.aspectRatio;
    }
    // Both insets set and size auto: the box fills the gap between them.
    for (int a = 0; a < 2; ++a) {
      const Length& sz = a == 0 ? as.width : as.height;
      if (std::isnan(bsz[a]) && sz.kind == Length::Kind::Auto && !std::isnan(pos[a]) &&
          !std::isnan(endInset[a])) {
        bsz[a] = std::max(0.0, cb[a] - pos[a] - endInset[a] - mS[a] - mE[a]);
      }
    }
    if (std::isnan(bsz[0]) || std::isnan(bsz[1])) {
      SizeConstraint sc;
      for (int a = 0; a < 2; ++a) {
        AxisConstraint ac;
        ac.min = lo[a];
        ac.max = hi[a];
        if (!std::isnan(bsz[a])) {
          ac.mode = MeasureMode::Exactly;
          ac.size = clampMinWins(bsz[a], lo[a], hi[a]);
        } else {
          ac.mode = MeasureMode::AtMost;
          ac.size = std::max(0.0, cb[a] - (std::isnan(pos[a]) ? 0.0 : pos[a]) -
                                      (std::isnan(endInset[a]) ? 0.0 : endInset[a]) - mS[a] - mE[a]);
        }
        (a == 0 ? sc.w : sc.h) = ac;
      }
      const Size m = eng_.measure(aid, sc);
      if (std::isnan(bsz[0])) bsz[0] = m.w;
      if (std::isnan(bsz[1])) bsz[1] = m.h;
    }
    double out[2];
    for (int a = 0; a < 2; ++a) {
      bsz[a] = clampMinWins(bsz[a], lo[a], hi[a]);
      if (!std::isnan(pos[a])) {
        out[a] = pos[a] + mS[a];
      } else if (!std::isnan(endInset[a])) {
        out[a] = cb[a] - endInset[a] - bsz[a] - mE[a];
      } else {
        const double free = inner_[a] - bsz[a] - mS[a] - mE[a];
        double off;
        if (a == M_) {
          off = justifyOffset(free);
        } else {
          Align al = as.alignSelf == Align::Auto ? s_.alignItems : as.alignSelf;
          off = alignOffset(al, free);
          if (crossRev_) off = free - off;
        }
        out[a] = padStart_[a] + off + mS[a];
      }
    }
    w->exact.x = clampExtent(out[0]);
    w->exact.y = clampExtent(out[1]);
    eng_.commit(aid, bsz[0], bsz[1]);
  }

  void placeAndCommit() {
    distributeLines();
    for (const Line& ln : lines_) placeLine(ln);
    for (Item& it : flow_) commitChild(it);
    for (const tree::WidgetId aid : absolute_) placeAbsolute(aid);
    for (const tree::WidgetId hid : hidden_) {
      if (tree::Widget* h = eng_.tree().get(hid)) {
        h->exact.x = padStart_[0];
        h->exact.y = padStart_[1];
      }
      eng_.commit(hid, 0.0, 0.0);
    }
  }

  Engine& eng_;
  tree::WidgetId id_;
  const Style s_;
  bool commit_;
  bool row_ = true;
  bool mainRev_ = false;
  bool crossRev_ = false;
  bool wrap_ = false;
  int M_ = 0;
  int X_ = 1;
  AxisConstraint ax_[2];
  double padStart_[2] = {0, 0};
  double pad_[2] = {0, 0};
  double minEff_[2] = {0, 0};
  double maxEff_[2] = {kMaxExtent, kMaxExtent};
  bool def_[2] = {false, false};
  double outer_[2] = {0, 0};
  double inner_[2] = {kNaN, kNaN};
  double gapM_ = 0;
  double gapX_ = 0;
  double availX_ = kNaN;
  std::vector<Item> flow_;
  std::vector<tree::WidgetId> absolute_;
  std::vector<tree::WidgetId> hidden_;
  std::vector<Line> lines_;
};

}  // namespace

// The run lives on the heap: layout recurses once per tree level (up to kMaxSupportedDepth), and
// keeping the large per-container state off the stack bounds stack use per level to a few
// hundred bytes.
Size runFlexContainer(Engine& engine, tree::WidgetId id, const SizeConstraint& c, bool commit) {
  const std::unique_ptr<FlexRun> run = std::make_unique<FlexRun>(engine, id, c, commit);
  return run->run();
}

}  // namespace r1ui::core::layout::detail
