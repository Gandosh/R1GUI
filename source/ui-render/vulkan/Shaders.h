// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: declarations of the embedded SPIR-V blobs.
// Why: the shaders in source/ui-render/shaders are compiled by glslc at build time and turned into
//   byte arrays by shaders/embed_spirv.cmake; the generated sources define these symbols.
// Callers: vulkan/Pipelines.cpp (module creation) and the generated *.spv.cpp files.
// Invariant: data is 4-byte aligned and size is a multiple of 4, as vkCreateShaderModule requires.
#pragma once

#include <cstddef>

namespace r1ui::render::detail {

struct ShaderBlob {
  const unsigned char* data;
  size_t size;
};

extern const ShaderBlob kSdfVertSpv;
extern const ShaderBlob kSdfFragSpv;
extern const ShaderBlob kTexturedVertSpv;
extern const ShaderBlob kTexturedFragSpv;

}  // namespace r1ui::render::detail
