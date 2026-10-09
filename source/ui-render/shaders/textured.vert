// Owns: vertex stage of the textured-quad pipeline (glyph coverage and RGBA images).
// Why: one quad per instance from gl_VertexIndex; instance layout mirrors TexInstance in
//   include/r1ui/render/PaintList.h (4 x vec4, per-instance rate).
// Space: physical pixels, origin top-left, y down; push constant = target size.
#version 450

layout(location = 0) in vec4 inDst;   // x, y, w, h
layout(location = 1) in vec4 inUv;    // u0, v0, u1, v1
layout(location = 2) in vec4 inTint;
layout(location = 3) in vec4 inParams;

layout(push_constant) uniform Constants {
  vec2 viewport;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out vec4 fTint;
layout(location = 2) flat out float fMode;

const vec2 kCorners[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
                                 vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
  const vec2 corner = kCorners[gl_VertexIndex];
  const vec2 pos = inDst.xy + inDst.zw * corner;
  gl_Position = vec4(pos / pc.viewport * 2.0 - 1.0, 0.0, 1.0);
  vUv = mix(inUv.xy, inUv.zw, corner);
  fTint = inTint;
  fMode = inParams.x;
}
