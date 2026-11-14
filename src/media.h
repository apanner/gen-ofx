// Image/video encode-decode with Windows' built-in codecs (WIC + Media Foundation).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace media {

// 8-bit RGB frame, rows top-to-bottom.
struct Frame {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

// Single-channel float image in 0-1, rows top-to-bottom.
struct Gray {
    int w = 0, h = 0;
    std::vector<float> px;
};

bool init(std::string& err);  // COM + Media Foundation, once per process

bool writePNG(const std::wstring& path, const Frame& f, std::string& err);
bool writePNG(const std::wstring& path, const Gray& g, std::string& err);
bool writeEXR(const std::wstring& path, const Frame& f, std::string& err);
bool writeEXR(const std::wstring& path, const Gray& g, std::string& err);

struct ExrLayer {
    std::string name;            // Nuke layer prefix (e.g. "hero", "car")
    const Frame* rgb = nullptr;  // optional per-layer RGB (if null, uses baseRgb premultiplied by alpha)
    const Gray* alpha = nullptr; // required per-layer alpha
};

// Write a multi-layer 16-bit half scanline EXR (base rgba + named <layer>.rgba channels).
bool writeMultipassEXR(const std::wstring& path, const Frame* baseRgb, const Gray* baseAlpha,
                       const std::vector<ExrLayer>& layers, std::string& err);

bool writeImage(const std::wstring& path, const Frame& f, std::string& err);
bool writeImage(const std::wstring& path, const Gray& g, std::string& err);

// Decode a PNG (8 or 16 bit, grey/RGB/RGBA) from memory; returns the red channel.
bool decodePNG(const std::string& bytes, Gray& out, std::string& err);
// Same, keeping RGB (8-bit) -- clean plates coming back from ComfyUI.
bool decodePNG(const std::string& bytes, Frame& out, std::string& err);

// Encode frames to an H.264 MP4 at the given fps. Dimensions must be even.
bool writeMP4(const std::wstring& path, const std::vector<const Frame*>& frames, int fps, std::string& err);

}  // namespace media
