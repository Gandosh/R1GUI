# Third-party register

Rule: every dependency is recorded here with license and exact version before first use. The approved
initial list is in [Goal/evidence/P0_S07.md](../Goal/evidence/P0_S07.md). Nothing is vendored yet.

| Library | Version | License | Source | Used by | Status |
|---|---|---|---|---|---|
| Vulkan SDK (headers, loader, validation layers, glslc) | 1.4.363.0 | Apache-2.0 (SDK components carry their own licences in `C:\VulkanSDK\1.4.363.0\Licenses`) | LunarG installer `vulkansdk-windows-X64-1.4.363.0.exe` (signature valid, 2026-10-09) | ui-render | installed machine-wide; not vendored |
| FreeType | 2.14.3 | FTL (chosen option; not GPL): third_party/freetype/docs/FTL.TXT, LICENSE.TXT | freetype-2.14.3.tar.xz from download.savannah.gnu.org (mirror), sha256 36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f, approved by the owner 2026-10-09 | ui-text glyph rasterization | vendored in third_party/freetype (src, include, builds, CMake files only) |
| HarfBuzz | 14.6.0 | MIT (third_party/harfbuzz/COPYING) | harfbuzz-14.6.0.tar.xz from github.com/harfbuzz/harfbuzz releases, sha256 d07a007327277708a2a73ae437887cdbaf282937f6d03ca5467723e9099af586, approved by the owner 2026-10-09 | ui-text shaping | vendored in third_party/harfbuzz (src only; project docs and tooling files not copied) |
| Inter fonts (Regular, Medium, SemiBold, Bold, ExtraBold) | 4.001 | SIL OFL 1.1 (`assets/fonts/OFL.txt`; copyright The Inter Project Authors) | copied from the OpenPencil `public/` folder | ui-text, preview | vendored in `assets/fonts/` |
| Lucide icons (156 SVGs extracted) | version of the OpenPencil build, not recorded | ISC (`assets/icons/lucide/LICENSE`, text written from the published notice; verify against the upstream file before release) | extracted from the OpenPencil built bundle | ui-render icon atlas | vendored in `assets/icons/lucide/` |

## Reference material (not dependencies)
- OpenPencil, MIT, Copyright (c) 2026 Danila Poyarkov: design tokens and visual values only, notice retained. Checkout at `reference/OpenPencil` (git-ignored, never a build input). See [Goal/evidence/P0_S08.md](../Goal/evidence/P0_S08.md).
