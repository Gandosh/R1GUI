# Phase 0 close record

**Status:** TECHNICAL PASS except 0.14; owner gate PENDING   **Date:** 2026-10-09

## Slices
| Slice | Status | Evidence |
|---|---|---|
| 0.1 EULA confirmation | OWNER ACCEPTED | [P0_S01](P0_S01.md) |
| 0.2 Vulkan, 0.3 Windows first | OWNER ACCEPTED | GOAL.md |
| 0.4 Own Win32 | OWNER ACCEPTED | [P0_S04](P0_S04.md) |
| 0.5 CMake + Ninja, C++20, MSVC | OWNER ACCEPTED | [P0_S05](P0_S05.md) |
| 0.6 FreeType + HarfBuzz | OWNER ACCEPTED | [P0_S06](P0_S06.md) |
| 0.7 Allowlist | OWNER ACCEPTED (initial list) | [P0_S07](P0_S07.md) |
| 0.8 OpenPencil MIT | TECHNICAL PASS | [P0_S08](P0_S08.md) |
| 0.9 Repo and license | OWNER ACCEPTED | [P0_S09](P0_S09.md) |
| 0.10-0.13 Repo, skeleton, wrappers, audit | TECHNICAL PASS | [S10](P0_S10.md) [S11](P0_S11.md) [S12](P0_S12.md) [S13](P0_S13.md) |
| 0.14 CI | BLOCKED (billing lock) | [P0_S14](P0_S14.md) |
| 0.15 Vulkan preview | TECHNICAL PASS | [P0_S15](P0_S15.md) |

## Launch the preview
`tools\run\preview.cmd` (or run `build\dev\bin\r1gui-preview.exe` after a build). Interaction checklist: [P0_S15](P0_S15.md).

## Owner decisions
| Decision | Date |
|---|---|
| Vulkan, Windows first, own Win32, CMake+Ninja+C++20+MSVC, FreeType+HarfBuzz, proprietary repo here, UE source via separate reader | 2026-10-09 |

## Accepted risks / open items
- CI never ran: the first run on GitHub was refused because the account has a billing issue. Owner clears it or names another CI host.
- Pushed: `main` at https://github.com/Gandosh/R1GUI (first commit 39ae6d5).
- No sanitizer preset yet (`sanitize.cmd` is a placeholder); no leak check of the preview.
- Vulkan SDK is installed machine-wide, not vendored; CI pins the same version.
- GPU choice in the preview is "first discrete" (currently the RTX 4080).

## Build notes
- Local-only `tools\build\env.local.cmd` (gitignored) sets `VULKAN_SDK`; example in `env.example.cmd`.
- Stale CMake cache after installing the SDK: reconfigure with `-UVulkan_*`.
- Failed installer attempts left empty `C:\VulkanSDK`-style folders and `J:\VulkanSDK`; `J:\Vulkan\1.4.363.0` may also exist empty. Safe to delete by the owner.
