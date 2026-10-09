// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Phase 4 performance baseline run (slice 4.17): CPU frame time of the Widgets and the
//   Gallery mode with forced redraw (and of each Gallery page), idle CPU and frame count (also with
//   a focused field), memory (working set, private bytes, GPU device-local) after idling and after
//   visiting the other modes, and startup time to the first frame.
// Why: later phases need numbers to compare against; this records them with the real application
//   code paths (PreviewApp::frame / step), not a separate mock loop.
// Callers: main.cpp (--bench <directory>). Output: <directory>/phase4_baseline.json and .md.
// The numbers are a baseline for regression checks on this machine, not a budget.
#pragma once

#include <filesystem>

namespace preview {

// Runs the whole baseline and writes the result files. Returns the process exit code. Throws
// std::runtime_error for unrecoverable problems (the shell shows them in a message box).
int runBench(const std::filesystem::path& outputDirectory);

}  // namespace preview
