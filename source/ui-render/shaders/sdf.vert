// Owns: vertex stage of the instanced SDF pipeline (fills, borders, shadows, lines).
// Why: one quad per instance, sized to cover the shape plus its anti-aliasing (or blur) margin;
//   the fragment stage evaluates the analytic coverage. Instance layout mirrors SdfInstance in
//   include/r1ui/render/PaintList.h (6 x vec4, per-instance rate). No vertex buffer: the corner is
//   derived from gl_VertexIndex (two triangles, 6 vertices).
// Space: positions are physical pixels, origin top-left, y down; the push constant is the target
//   size, so NDC y grows downward exactly like Vulkan framebuffer space (no flip).
#version 450

layout(location = 0) in vec4 inRect;
layout(location = 1) in vec4 inRadii;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inParams;
layout(location = 4) in vec4 inKnockRect;
layout(location = 5) in vec4 inKnockRadii;

layout(push_constant) uniform Constants {
  vec2 viewport;
} pc;

layout(location = 0) out vec2 vPos;
layout(location = 1) flat out vec4 fRect;
layout(location = 2) flat out vec4 fRadii;
layout(location = 3) flat out vec4 fColor;
layout(location = 4) flat out vec4 fParams;
layout(location = 5) flat out vec4 fKnockRect;
layout(location = 6) flat out vec4 fKnockRadii;

const vec2 kCorners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
                                 vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
  const float kind = inParams.x;
  vec2 lo;
  vec2 hi;
  if (kind > 2.5) {
    // Line: endpoints in rect.xy / rect.zw, padded by half the width plus the AA ramp.
    const float pad = inParams.y * 0.5 + 1.0;
    lo = min(inRect.xy, inRect.zw) - pad;
    hi = max(inRect.xy, inRect.zw) + pad;
  } else {
    // 1 px for the AA ramp; a shadow reaches three sigma beyond its box.
    const float pad = kind > 1.5 ? 3.0 * inParams.y + 1.0 : 1.0;
    lo = inRect.xy - pad;
    hi = inRect.xy + inRect.zw + pad;
  }
  const vec2 pos = mix(lo, hi, kCorners[gl_VertexIndex]);
  gl_Position = vec4(pos / pc.viewport * 2.0 - 1.0, 0.0, 1.0);
  vPos = pos;
  fRect = inRect;
  fRadii = inRadii;
  fColor = inColor;
  fParams = inParams;
  fKnockRect = inKnockRect;
  fKnockRadii = inKnockRadii;
}
