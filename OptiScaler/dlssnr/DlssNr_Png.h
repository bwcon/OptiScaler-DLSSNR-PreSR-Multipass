// Writes a mapped D3D12 readback surface out as a viewable 8-bit PNG.
//
// Added for the user-facing "NR on/off screenshot" feature (default key F5). The DLSS-NR capture path
// already holds two frames per shot -- the upscaler output before the model's edit and after it -- so
// this turns each into a PNG for a direct A/B look, instead of the raw dumps the measurement path writes.
//
// HDR/float surfaces are Reinhard tone-mapped and sRGB-encoded so the file is viewable. That is a fair
// comparison (both frames get the identical transform) but it is NOT the game's exact output transfer.
// UNORM surfaces are already display-encoded and are copied straight through.

#pragma once

#include <string>

namespace dlssnr_png
{
// dxgiFormat is a DXGI_FORMAT passed as int to keep this header light. mapped points at row-major pixel
// data with rowPitch bytes per row (the D3D12 placed-footprint pitch). Returns true if the PNG was written.
bool WritePng(const std::wstring& path, const void* mapped, unsigned int rowPitch, unsigned int width,
              unsigned int height, int dxgiFormat);
} // namespace dlssnr_png
