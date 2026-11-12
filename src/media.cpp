#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "media.h"

#include <mutex>

using Microsoft::WRL::ComPtr;

namespace media {

static std::string hr(const char* what, HRESULT h) {
    char b[160];
    snprintf(b, sizeof(b), "%s failed (0x%08lx)", what, (unsigned long)h);
    return b;
}

// COM must be initialised per thread; Media Foundation once per process.
static bool comInit() {
    HRESULT h = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    return SUCCEEDED(h) || h == RPC_E_CHANGED_MODE;  // host already set an apartment: fine
}

bool init(std::string& err) {
    static std::once_flag once;
    static HRESULT mf = S_OK;
    std::call_once(once, [] { mf = MFStartup(MF_VERSION, MFSTARTUP_FULL); });
    if (!comInit()) {
        err = "COM init failed";
        return false;
    }
    if (FAILED(mf)) {
        err = hr("MFStartup", mf);
        return false;
    }
    return true;
}

static ComPtr<IWICImagingFactory> factory(std::string& err) {
    ComPtr<IWICImagingFactory> f;
    comInit();
    HRESULT h = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    if (FAILED(h)) err = hr("WIC factory", h);
    return f;
}

bool writePNG(const std::wstring& path, const Frame& fr, std::string& err) {
    auto f = factory(err);
    if (!f) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT h = f->CreateStream(&stream);
    if (SUCCEEDED(h)) h = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(h)) h = f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(h)) h = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(h)) h = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(h)) h = frame->Initialize(nullptr);
    if (SUCCEEDED(h)) h = frame->SetSize(fr.w, fr.h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;  // what the PNG encoder accepts natively
    if (SUCCEEDED(h)) h = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(h) && fmt != GUID_WICPixelFormat24bppBGR) h = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    std::vector<BYTE> bgr(fr.rgb.size());
    for (size_t i = 0; i + 2 < bgr.size(); i += 3) {
        bgr[i] = fr.rgb[i + 2];
        bgr[i + 1] = fr.rgb[i + 1];
        bgr[i + 2] = fr.rgb[i];
    }
    if (SUCCEEDED(h)) h = frame->WritePixels(fr.h, fr.w * 3, (UINT)bgr.size(), bgr.data());
    if (SUCCEEDED(h)) h = frame->Commit();
    if (SUCCEEDED(h)) h = enc->Commit();
    if (FAILED(h)) {
        err = hr("PNG encode", h);
        return false;
    }
    return true;
}

bool writePNG(const std::wstring& path, const Gray& g, std::string& err) {
    auto f = factory(err);
    if (!f) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT h = f->CreateStream(&stream);
    if (SUCCEEDED(h)) h = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(h)) h = f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(h)) h = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(h)) h = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(h)) h = frame->Initialize(nullptr);
    if (SUCCEEDED(h)) h = frame->SetSize(g.w, g.h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat16bppGray;
    if (SUCCEEDED(h)) h = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(h) && fmt != GUID_WICPixelFormat16bppGray) {
        fmt = GUID_WICPixelFormat8bppGray;
        h = frame->SetPixelFormat(&fmt);
    }
    if (SUCCEEDED(h) && fmt == GUID_WICPixelFormat16bppGray) {
        std::vector<uint16_t> px16(g.px.size());
        for (size_t i = 0; i < g.px.size(); ++i) {
            float v = g.px[i] < 0.0f ? 0.0f : (g.px[i] > 1.0f ? 1.0f : g.px[i]);
            px16[i] = (uint16_t)(v * 65535.0f + 0.5f);
        }
        h = frame->WritePixels(g.h, g.w * (UINT)sizeof(uint16_t), (UINT)(px16.size() * sizeof(uint16_t)), (BYTE*)px16.data());
    } else if (SUCCEEDED(h)) {
        std::vector<uint8_t> px8(g.px.size());
        for (size_t i = 0; i < g.px.size(); ++i) {
            float v = g.px[i] < 0.0f ? 0.0f : (g.px[i] > 1.0f ? 1.0f : g.px[i]);
            px8[i] = (uint8_t)(v * 255.0f + 0.5f);
        }
        h = frame->WritePixels(g.h, g.w, (UINT)px8.size(), px8.data());
    }
    if (SUCCEEDED(h)) h = frame->Commit();
    if (SUCCEEDED(h)) h = enc->Commit();
    if (FAILED(h)) {
        err = hr("PNG encode (gray)", h);
        return false;
    }
    return true;
}

// Decode to 16-bit RGBA (keeps Alpha-Gen's 16-bit precision).
static bool decodeRGBA16(const std::string& bytes, std::vector<uint16_t>& buf, UINT& w, UINT& ht, std::string& err) {
    auto f = factory(err);
    if (!f) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    HRESULT h = f->CreateStream(&stream);
    if (SUCCEEDED(h)) h = stream->InitializeFromMemory((BYTE*)bytes.data(), (DWORD)bytes.size());
    if (SUCCEEDED(h)) h = f->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec);
    if (SUCCEEDED(h)) h = dec->GetFrame(0, &frame);
    w = ht = 0;
    if (SUCCEEDED(h)) h = frame->GetSize(&w, &ht);
    if (SUCCEEDED(h)) h = f->CreateFormatConverter(&conv);
    if (SUCCEEDED(h))
        h = conv->Initialize(frame.Get(), GUID_WICPixelFormat64bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                             WICBitmapPaletteTypeCustom);
    buf.assign((size_t)w * ht * 4, 0);
    if (SUCCEEDED(h)) h = conv->CopyPixels(nullptr, w * 8, (UINT)(buf.size() * 2), (BYTE*)buf.data());
    if (FAILED(h)) {
        err = hr("PNG decode", h);
        return false;
    }
    return true;
}

bool decodePNG(const std::string& bytes, Gray& out, std::string& err) {
    std::vector<uint16_t> buf;
    UINT w, ht;
    if (!decodeRGBA16(bytes, buf, w, ht, err)) return false;
    out.w = (int)w;
    out.h = (int)ht;
    out.px.resize((size_t)w * ht);
    for (size_t i = 0; i < out.px.size(); ++i) out.px[i] = buf[i * 4] / 65535.f;
    return true;
}

bool decodePNG(const std::string& bytes, Frame& out, std::string& err) {
    std::vector<uint16_t> buf;
    UINT w, ht;
    if (!decodeRGBA16(bytes, buf, w, ht, err)) return false;
    out.w = (int)w;
    out.h = (int)ht;
    out.rgb.resize((size_t)w * ht * 3);
    for (size_t i = 0; i < (size_t)w * ht; ++i)
        for (int c = 0; c < 3; ++c) out.rgb[i * 3 + c] = (uint8_t)((buf[i * 4 + c] + 128) / 257);
    return true;
}

bool writeMP4(const std::wstring& path, const std::vector<const Frame*>& frames, int fps, std::string& err) {
    if (frames.empty()) {
        err = "no frames to encode";
        return false;
    }
    if (!init(err)) return false;
    const UINT32 w = frames[0]->w, h = frames[0]->h;
    ComPtr<IMFAttributes> attrs;
    ComPtr<IMFSinkWriter> writer;
    ComPtr<IMFMediaType> outType, inType;
    DWORD stream = 0;
    HRESULT r = MFCreateAttributes(&attrs, 2);
    if (SUCCEEDED(r)) r = attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);  // deterministic SW encoder
    if (SUCCEEDED(r)) r = attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (SUCCEEDED(r)) r = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs.Get(), &writer);
    // output: H.264 High, high bitrate (the models see it; keep it visually lossless)
    UINT32 bitrate = (UINT32)std::min<double>(80e6, (double)w * h * fps * 1.2);
    if (SUCCEEDED(r)) r = MFCreateMediaType(&outType);
    if (SUCCEEDED(r)) r = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(r)) r = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(r)) r = outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    if (SUCCEEDED(r)) r = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(r)) r = outType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    if (SUCCEEDED(r)) r = MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, w, h);
    if (SUCCEEDED(r)) r = MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, fps, 1);
    if (SUCCEEDED(r)) r = MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(r)) r = writer->AddStream(outType.Get(), &stream);
    // input: top-down RGB32 (BGRA); the sink writer inserts the colour converter
    if (SUCCEEDED(r)) r = MFCreateMediaType(&inType);
    if (SUCCEEDED(r)) r = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(r)) r = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(r)) r = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(r)) r = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, w * 4);
    if (SUCCEEDED(r)) r = MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, w, h);
    if (SUCCEEDED(r)) r = MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, fps, 1);
    if (SUCCEEDED(r)) r = MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(r)) r = writer->SetInputMediaType(stream, inType.Get(), nullptr);
    if (SUCCEEDED(r)) r = writer->BeginWriting();
    if (FAILED(r)) {
        err = hr("H.264 encoder setup", r);
        return false;
    }
    const LONGLONG dur = 10000000LL / fps;
    for (size_t i = 0; i < frames.size() && SUCCEEDED(r); ++i) {
        const Frame& f = *frames[i];
        ComPtr<IMFMediaBuffer> buf;
        ComPtr<IMFSample> sample;
        r = MFCreateMemoryBuffer(w * h * 4, &buf);
        BYTE* dst = nullptr;
        if (SUCCEEDED(r)) r = buf->Lock(&dst, nullptr, nullptr);
        if (SUCCEEDED(r)) {
            for (size_t p = 0, n = (size_t)w * h; p < n; ++p) {
                dst[p * 4 + 0] = f.rgb[p * 3 + 2];
                dst[p * 4 + 1] = f.rgb[p * 3 + 1];
                dst[p * 4 + 2] = f.rgb[p * 3 + 0];
                dst[p * 4 + 3] = 255;
            }
            buf->Unlock();
            r = buf->SetCurrentLength(w * h * 4);
        }
        if (SUCCEEDED(r)) r = MFCreateSample(&sample);
        if (SUCCEEDED(r)) r = sample->AddBuffer(buf.Get());
        if (SUCCEEDED(r)) r = sample->SetSampleTime((LONGLONG)i * dur);
        if (SUCCEEDED(r)) r = sample->SetSampleDuration(dur);
        if (SUCCEEDED(r)) r = writer->WriteSample(stream, sample.Get());
    }
    if (SUCCEEDED(r)) r = writer->Finalize();
    if (FAILED(r)) {
        err = hr("H.264 encode", r);
        return false;
    }
    return true;
}

// ------------------------------------------------------------- OpenEXR Writer (Native 16-bit Half)

static inline uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = ((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = (x & 0x7fffff) >> 13;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7c00);
    return (uint16_t)(sign | (exp << 10) | mant);
}

static void addExrAttr(std::vector<uint8_t>& hdr, const char* name, const char* type, const void* data, uint32_t size) {
    while (*name) hdr.push_back((uint8_t)*name++);
    hdr.push_back(0);
    while (*type) hdr.push_back((uint8_t)*type++);
    hdr.push_back(0);
    const uint8_t* sz = (const uint8_t*)&size;
    hdr.insert(hdr.end(), sz, sz + 4);
    const uint8_t* d = (const uint8_t*)data;
    hdr.insert(hdr.end(), d, d + size);
}

bool writeEXR(const std::wstring& path, const Frame& fr, std::string& err) {
    if (fr.w <= 0 || fr.h <= 0 || fr.rgb.empty()) {
        err = "Invalid frame dimensions for EXR";
        return false;
    }
    int w = fr.w, h = fr.h;
    std::vector<uint8_t> hdr;
    uint32_t magic = 0x01312f76, ver = 2;
    const uint8_t* m = (const uint8_t*)&magic; hdr.insert(hdr.end(), m, m + 4);
    const uint8_t* v = (const uint8_t*)&ver; hdr.insert(hdr.end(), v, v + 4);

    std::vector<uint8_t> chList;
    for (const char* ch : {"B", "G", "R"}) {
        while (*ch) chList.push_back((uint8_t)*ch++);
        chList.push_back(0);
        int32_t ptype = 1; // 1 = HALF
        const uint8_t* pt = (const uint8_t*)&ptype; chList.insert(chList.end(), pt, pt + 4);
        chList.push_back(0); chList.push_back(0); chList.push_back(0); chList.push_back(0);
        int32_t xSamp = 1, ySamp = 1;
        const uint8_t* xs = (const uint8_t*)&xSamp; chList.insert(chList.end(), xs, xs + 4);
        const uint8_t* ys = (const uint8_t*)&ySamp; chList.insert(chList.end(), ys, ys + 4);
    }
    chList.push_back(0);
    addExrAttr(hdr, "channels", "chlist", chList.data(), (uint32_t)chList.size());

    int8_t comp = 0; // NO_COMPRESSION
    addExrAttr(hdr, "compression", "compression", &comp, 1);

    int32_t box[4] = {0, 0, w - 1, h - 1};
    addExrAttr(hdr, "dataWindow", "box2i", box, 16);
    addExrAttr(hdr, "displayWindow", "box2i", box, 16);

    int8_t lineOrder = 0;
    addExrAttr(hdr, "lineOrder", "lineOrder", &lineOrder, 1);

    float par = 1.0f;
    addExrAttr(hdr, "pixelAspectRatio", "float", &par, 4);

    float center[2] = {0.0f, 0.0f};
    addExrAttr(hdr, "screenWindowCenter", "v2f", center, 8);

    float swidth = 1.0f;
    addExrAttr(hdr, "screenWindowWidth", "float", &swidth, 4);

    hdr.push_back(0);

    uint64_t tablePos = hdr.size();
    uint64_t lineDataStart = tablePos + (uint64_t)h * 8;
    uint32_t rowDataSize = (uint32_t)w * 2 * 3;
    uint64_t scanlineTotal = 4 + 4 + rowDataSize;

    std::vector<uint64_t> offsets(h);
    for (int y = 0; y < h; ++y) offsets[y] = lineDataStart + (uint64_t)y * scanlineTotal;

    FILE* fp = _wfopen(path.c_str(), L"wb");
    if (!fp) {
        err = "Could not create EXR file";
        return false;
    }
    fwrite(hdr.data(), 1, hdr.size(), fp);
    fwrite(offsets.data(), 8, h, fp);

    std::vector<uint16_t> rowB(w), rowG(w), rowR(w);
    for (int y = 0; y < h; ++y) {
        int32_t lineNum = y;
        fwrite(&lineNum, 4, 1, fp);
        fwrite(&rowDataSize, 4, 1, fp);
        const uint8_t* srcRow = &fr.rgb[(size_t)y * w * 3];
        for (int x = 0; x < w; ++x) {
            float r = srcRow[x * 3 + 0] / 255.0f;
            float g = srcRow[x * 3 + 1] / 255.0f;
            float b = srcRow[x * 3 + 2] / 255.0f;
            rowR[x] = floatToHalf(r);
            rowG[x] = floatToHalf(g);
            rowB[x] = floatToHalf(b);
        }
        fwrite(rowB.data(), 2, w, fp);
        fwrite(rowG.data(), 2, w, fp);
        fwrite(rowR.data(), 2, w, fp);
    }
    fclose(fp);
    return true;
}

bool writeEXR(const std::wstring& path, const Gray& g, std::string& err) {
    if (g.w <= 0 || g.h <= 0 || g.px.empty()) {
        err = "Invalid gray dimensions for EXR";
        return false;
    }
    int w = g.w, h = g.h;
    std::vector<uint8_t> hdr;
    uint32_t magic = 0x01312f76, ver = 2;
    const uint8_t* m = (const uint8_t*)&magic; hdr.insert(hdr.end(), m, m + 4);
    const uint8_t* v = (const uint8_t*)&ver; hdr.insert(hdr.end(), v, v + 4);

    std::vector<uint8_t> chList;
    const char* ch = "A";
    while (*ch) chList.push_back((uint8_t)*ch++);
    chList.push_back(0);
    int32_t ptype = 1; // HALF
    const uint8_t* pt = (const uint8_t*)&ptype; chList.insert(chList.end(), pt, pt + 4);
    chList.push_back(0); chList.push_back(0); chList.push_back(0); chList.push_back(0);
    int32_t xSamp = 1, ySamp = 1;
    const uint8_t* xs = (const uint8_t*)&xSamp; chList.insert(chList.end(), xs, xs + 4);
    const uint8_t* ys = (const uint8_t*)&ySamp; chList.insert(chList.end(), ys, ys + 4);
    chList.push_back(0);
    addExrAttr(hdr, "channels", "chlist", chList.data(), (uint32_t)chList.size());

    int8_t comp = 0;
    addExrAttr(hdr, "compression", "compression", &comp, 1);
    int32_t box[4] = {0, 0, w - 1, h - 1};
    addExrAttr(hdr, "dataWindow", "box2i", box, 16);
    addExrAttr(hdr, "displayWindow", "box2i", box, 16);
    int8_t lineOrder = 0;
    addExrAttr(hdr, "lineOrder", "lineOrder", &lineOrder, 1);
    float par = 1.0f;
    addExrAttr(hdr, "pixelAspectRatio", "float", &par, 4);
    float center[2] = {0.0f, 0.0f};
    addExrAttr(hdr, "screenWindowCenter", "v2f", center, 8);
    float swidth = 1.0f;
    addExrAttr(hdr, "screenWindowWidth", "float", &swidth, 4);
    hdr.push_back(0);

    uint64_t tablePos = hdr.size();
    uint64_t lineDataStart = tablePos + (uint64_t)h * 8;
    uint32_t rowDataSize = (uint32_t)w * 2;
    uint64_t scanlineTotal = 4 + 4 + rowDataSize;

    std::vector<uint64_t> offsets(h);
    for (int y = 0; y < h; ++y) offsets[y] = lineDataStart + (uint64_t)y * scanlineTotal;

    FILE* fp = _wfopen(path.c_str(), L"wb");
    if (!fp) {
        err = "Could not create EXR file";
        return false;
    }
    fwrite(hdr.data(), 1, hdr.size(), fp);
    fwrite(offsets.data(), 8, h, fp);

    std::vector<uint16_t> rowA(w);
    for (int y = 0; y < h; ++y) {
        int32_t lineNum = y;
        fwrite(&lineNum, 4, 1, fp);
        fwrite(&rowDataSize, 4, 1, fp);
        const float* srcRow = &g.px[(size_t)y * w];
        for (int x = 0; x < w; ++x) {
            rowA[x] = floatToHalf(srcRow[x]);
        }
        fwrite(rowA.data(), 2, w, fp);
    }
    fclose(fp);
    return true;
}

bool writeImage(const std::wstring& path, const Frame& f, std::string& err) {
    if (path.size() >= 4 && (_wcsicmp(path.c_str() + path.size() - 4, L".exr") == 0)) {
        return writeEXR(path, f, err);
    }
    return writePNG(path, f, err);
}

bool writeImage(const std::wstring& path, const Gray& g, std::string& err) {
    if (path.size() >= 4 && (_wcsicmp(path.c_str() + path.size() - 4, L".exr") == 0)) {
        return writeEXR(path, g, err);
    }
    return writePNG(path, g, err);
}

}  // namespace media
