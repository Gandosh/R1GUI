# Third-party register

Rule: every dependency is recorded here with license and exact version before first use. The approved
initial list is in [Goal/evidence/P0_S07.md](../Goal/evidence/P0_S07.md). Nothing is vendored yet.

| Library | Version | License | Source | Used by | Status |
|---|---|---|---|---|---|
| Vulkan SDK (headers, loader, validation layers, glslc) | 1.4.363.0 | Apache-2.0 (SDK components carry their own licences in `C:\VulkanSDK\1.4.363.0\Licenses`) | LunarG installer `vulkansdk-windows-X64-1.4.363.0.exe` (signature valid, 2026-10-09) | ui-render | installed machine-wide; not vendored |

## Reference material (not dependencies)
- OpenPencil, MIT, Copyright (c) 2026 Danila Poyarkov: design tokens and visual values only, notice retained. Checkout at `reference/OpenPencil` (git-ignored, never a build input). See [Goal/evidence/P0_S08.md](../Goal/evidence/P0_S08.md).
