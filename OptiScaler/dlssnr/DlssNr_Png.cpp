#include "DlssNr_Png.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../external/FidelityFX-SDK/framework/cauldron/framework/libs/stb/stb_image_write.h"

#include <windows.h>
#include <dxgiformat.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (uint32_t) (h & 0x8000) << 16;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t mant = h & 0x3FF;
    uint32_t bits;

    if (exp == 0)
    {
        if (mant == 0)
        {
            bits = sign; // +/- zero
        }
        else
        {
            // Subnormal: normalise it.
            int e = -1;
            uint32_t m = mant;
            do
            {
                ++e;
                m <<= 1;
            } while ((m & 0x400) == 0);
            m &= 0x3FF;
            bits = sign | ((uint32_t) (127 - 15 - e) << 23) | (m << 13);
        }
    }
    else if (exp == 0x1F)
    {
        bits = sign | 0x7F800000u | (mant << 13); // Inf / NaN
    }
    else
    {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }

    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Linear (already tone-mapped to [0,1]) -> 8-bit sRGB.
uint8_t LinToSrgb8(float l)
{
    if (l <= 0.0f)
        return 0;
    if (l >= 1.0f)
        l = 1.0f;

    const float s = (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f);
    int v = (int) (s * 255.0f + 0.5f);
    return (uint8_t) (v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Reinhard maps open-ended linear HDR into [0,1) without clipping bright detail; then sRGB for display.
uint8_t HdrToSrgb8(float linear)
{
    if (!(linear > 0.0f)) // also catches NaN
        return 0;
    const float mapped = linear / (1.0f + linear);
    return LinToSrgb8(mapped);
}

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t) n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int) w.size(), out.data(), n, nullptr, nullptr);
    return out;
}
} // namespace

namespace dlssnr_png
{
bool WritePng(const std::wstring& path, const void* mapped, unsigned int rowPitch, unsigned int width,
              unsigned int height, int dxgiFormat)
{
    if (mapped == nullptr || width == 0 || height == 0 || rowPitch == 0)
        return false;

    std::vector<uint8_t> rgba((size_t) width * height * 4);
    const uint8_t* base = (const uint8_t*) mapped;
    const auto fmt = (DXGI_FORMAT) dxgiFormat;

    for (unsigned int y = 0; y < height; ++y)
    {
        const uint8_t* row = base + (size_t) y * rowPitch;
        uint8_t* out = rgba.data() + (size_t) y * width * 4;

        for (unsigned int x = 0; x < width; ++x)
        {
            uint8_t r = 0, g = 0, b = 0;

            switch (fmt)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            {
                const uint8_t* p = row + (size_t) x * 4;
                r = p[0]; g = p[1]; b = p[2];
                break;
            }
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            {
                const uint8_t* p = row + (size_t) x * 4;
                b = p[0]; g = p[1]; r = p[2];
                break;
            }
            case DXGI_FORMAT_R10G10B10A2_UNORM:
            {
                uint32_t v;
                std::memcpy(&v, row + (size_t) x * 4, 4);
                const uint32_t r10 = v & 0x3FF, g10 = (v >> 10) & 0x3FF, b10 = (v >> 20) & 0x3FF;
                r = (uint8_t) ((r10 * 255 + 511) / 1023);
                g = (uint8_t) ((g10 * 255 + 511) / 1023);
                b = (uint8_t) ((b10 * 255 + 511) / 1023);
                break;
            }
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            {
                const uint16_t* p = (const uint16_t*) (row + (size_t) x * 8);
                r = HdrToSrgb8(HalfToFloat(p[0]));
                g = HdrToSrgb8(HalfToFloat(p[1]));
                b = HdrToSrgb8(HalfToFloat(p[2]));
                break;
            }
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
            {
                const float* p = (const float*) (row + (size_t) x * 16);
                r = HdrToSrgb8(p[0]);
                g = HdrToSrgb8(p[1]);
                b = HdrToSrgb8(p[2]);
                break;
            }
            case DXGI_FORMAT_R11G11B10_FLOAT:
            {
                uint32_t v;
                std::memcpy(&v, row + (size_t) x * 4, 4);
                // 11/11/10 float: 5-bit exponents, no sign. Reuse the half decoder by repacking to half bits.
                auto f11 = [](uint32_t m11) {
                    uint16_t h = (uint16_t) ((m11 & 0x7FF) << 4); // 6 mantissa -> shift into half's 10, exp aligns
                    return HalfToFloat(h);
                };
                auto f10 = [](uint32_t m10) {
                    uint16_t h = (uint16_t) ((m10 & 0x3FF) << 5);
                    return HalfToFloat(h);
                };
                r = HdrToSrgb8(f11(v & 0x7FF));
                g = HdrToSrgb8(f11((v >> 11) & 0x7FF));
                b = HdrToSrgb8(f10((v >> 22) & 0x3FF));
                break;
            }
            default:
                return false; // unknown format: caller keeps the raw path
            }

            out[(size_t) x * 4 + 0] = r;
            out[(size_t) x * 4 + 1] = g;
            out[(size_t) x * 4 + 2] = b;
            out[(size_t) x * 4 + 3] = 255; // opaque; game alpha is not meaningful for a screenshot
        }
    }

    const std::string utf8 = ToUtf8(path);
    return stbi_write_png(utf8.c_str(), (int) width, (int) height, 4, rgba.data(), (int) width * 4) != 0;
}
} // namespace dlssnr_png
