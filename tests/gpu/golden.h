#pragma once

#include <memory>
#include <ostream>  // doctest prints values (e.g. shared_ptr) with <<
#include <string>

#include <doctest.h>

#include "wlw/scene/viewport.h"

// Golden-image tests: a scene is drawn into an offscreen viewport of a hidden window and its pixels
// are compared with tests/gpu/references/<name>.png (support/image_compare.h).
//
// The references come from Mesa's llvmpipe (a software GPU: the same pixels on every run), which is
// what CI runs on (xvfb). On llvmpipe the comparison is strict; on a real GPU it's looser, because
// drivers round, filter and rasterize edges a little differently.
//
// On a failure, <build>/tests/gpu/gpu_output/ gets <name>.actual.png and <name>.diff.png (bad pixels
// in red). `wlw_gpu_tests --update` rewrites the references from this machine's GPU: run it on
// llvmpipe (or take CI's gpu_output artifact) so they stay the strict ones.
namespace wlw::gpu_tests {

inline constexpr int kSize = 256;  // every test picture is kSize x kSize pixels

// A viewport of kSize x kSize with a depth buffer, cleared to a dark opaque blue.
std::shared_ptr<scene::Viewport> MakeViewport();

// Draws `viewport` once and compares it with the reference `name`. Fails the test case on a
// mismatch, a missing reference or no GL context.
void CheckGolden(const std::string& name, const std::shared_ptr<scene::Viewport>& viewport);

// Set by main from --update.
void SetUpdateReferences(bool update);

}  // namespace wlw::gpu_tests
