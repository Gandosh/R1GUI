// Owns: fragment stage of the textured-quad pipeline.
// Why: mode 0 (coverage) multiplies the tint alpha by the R8 sample, which is how glyph atlases
//   are tinted; mode 1 (colour) multiplies an sRGB-encoded RGBA texel by the tint. Output is
//   premultiplied (blend ONE, ONE_MINUS_SRC_ALPHA); no sRGB conversion happens anywhere, textures
//   hold encoded values and blend in encoded space like the SDF pipeline.
#version 450

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in vec4 fTint;
layout(location = 2) flat in float fMode;

layout(location = 0) out vec4 outColor;

void main() {
  const vec4 texel = texture(uTexture, vUv);
  if (fMode < 0.5) {
    const float a = fTint.a * texel.r;
    outColor = vec4(fTint.rgb * a, a);
  } else {
    const float a = fTint.a * texel.a;
    outColor = vec4(fTint.rgb * texel.rgb * a, a);
  }
}
