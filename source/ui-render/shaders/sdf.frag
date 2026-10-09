// Owns: fragment stage of the instanced SDF pipeline: analytic coverage for fills, borders, CSS
//   box shadows and lines.
// Why: coverage comes from a signed distance sampled at the pixel centre, so an edge on a pixel
//   boundary is exactly 0/1 and any other edge gets a 1 px linear ramp. The double-precision twin
//   is r1ui::render::reference (SdfReference.cpp); keep the two formulas identical.
// Colour: values are sRGB-encoded and blended as-is (blend state ONE, ONE_MINUS_SRC_ALPHA with
//   premultiplied output), which reproduces browser compositing of CSS colours.
#version 450

layout(location = 0) in vec2 vPos;
layout(location = 1) flat in vec4 fRect;
layout(location = 2) flat in vec4 fRadii;
layout(location = 3) flat in vec4 fColor;
layout(location = 4) flat in vec4 fParams;
layout(location = 5) flat in vec4 fKnockRect;
layout(location = 6) flat in vec4 fKnockRadii;

layout(location = 0) out vec4 outColor;

// Signed distance to a rounded box (rect = x, y, w, h; radii = tl, tr, br, bl).
float sdRoundedBox(vec2 p, vec4 rect, vec4 radii) {
  const vec2 hs = rect.zw * 0.5;
  const vec2 q = p - (rect.xy + hs);
  const float r = q.x < 0.0 ? (q.y < 0.0 ? radii.x : radii.w) : (q.y < 0.0 ? radii.y : radii.z);
  const vec2 d = abs(q) - hs + r;
  return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

float coverage(float d) { return clamp(0.5 - d, 0.0, 1.0); }

// erf with max error 1.5e-7 (Abramowitz and Stegun 7.1.26).
float erfApprox(float x) {
  const float a = abs(x);
  const float t = 1.0 / (1.0 + 0.3275911 * a);
  const float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t +
                         0.254829592) * t * exp(-a * a);
  return x < 0.0 ? -y : y;
}

// Horizontal inset of a corner arc at height y inside a box of hs height hy.
float cornerInset(float y, float radius, float hy) {
  const float t = radius - (hy - abs(y));
  return (radius > 0.0 && t > 0.0) ? radius - sqrt(max(radius * radius - t * t, 0.0)) : 0.0;
}

// Gaussian-blurred rounded box: integrates, over rows within three sigma, the gaussian weight of
// the row times the erf-based horizontal coverage of that row.
float blurredBox(vec2 p, vec4 rect, vec4 radii, float sigma) {
  const vec2 hs = rect.zw * 0.5;
  const vec2 q = p - (rect.xy + hs);
  const float lo = max(q.y - 3.0 * sigma, -hs.y);
  const float hi = min(q.y + 3.0 * sigma, hs.y);
  if (hi <= lo) return 0.0;
  const int kSamples = 16;
  const float dy = (hi - lo) / float(kSamples);
  const float invSigmaRoot2 = 1.0 / (sigma * 1.41421356);
  float sum = 0.0;
  for (int i = 0; i < kSamples; ++i) {
    const float y = lo + (float(i) + 0.5) * dy;
    const float left = -hs.x + cornerInset(y, y < 0.0 ? radii.x : radii.w, hs.y);
    const float right = hs.x - cornerInset(y, y < 0.0 ? radii.y : radii.z, hs.y);
    const float row = 0.5 * (erfApprox((right - q.x) * invSigmaRoot2) - erfApprox((left - q.x) * invSigmaRoot2));
    const float w = exp(-0.5 * (q.y - y) * (q.y - y) / (sigma * sigma));
    sum += row * w;
  }
  // Normalise by the gaussian mass of the full +-3 sigma window (0.99730 of the whole line) so a
  // fully covered pixel reaches exactly 1; the truncated tail is below 0.3 percent of alpha.
  return clamp(sum * dy / (sigma * 2.50662827) / 0.9973, 0.0, 1.0);
}

float lineCoverage(vec2 p, vec2 a, vec2 b, float width) {
  const vec2 axis = b - a;
  const float len = length(axis);
  const vec2 u = axis / len;
  const vec2 q = p - (a + b) * 0.5;
  const vec2 d = vec2(abs(dot(q, u)) - len * 0.5, abs(dot(q, vec2(-u.y, u.x))) - width * 0.5);
  return coverage(length(max(d, 0.0)) + min(max(d.x, d.y), 0.0));
}

void main() {
  const vec2 p = gl_FragCoord.xy;
  const float kind = fParams.x;
  float a;
  if (kind < 0.5) {
    a = coverage(sdRoundedBox(p, fRect, fRadii));
  } else if (kind < 1.5) {
    const float w = fParams.y;
    const vec4 inner = vec4(fRect.xy + w, fRect.zw - 2.0 * w);
    float innerCoverage = 0.0;
    if (inner.z > 0.0 && inner.w > 0.0) {
      innerCoverage = coverage(sdRoundedBox(p, inner, max(fRadii - w, 0.0)));
    }
    a = coverage(sdRoundedBox(p, fRect, fRadii)) * (1.0 - innerCoverage);
  } else if (kind < 2.5) {
    const float sigma = fParams.y;
    const float body = sigma > 0.0 ? blurredBox(p, fRect, fRadii, sigma)
                                   : coverage(sdRoundedBox(p, fRect, fRadii));
    a = body * (1.0 - coverage(sdRoundedBox(p, fKnockRect, fKnockRadii)));
  } else {
    a = lineCoverage(p, fRect.xy, fRect.zw, fParams.y);
  }
  a *= fColor.a;
  outColor = vec4(fColor.rgb * a, a);
}
