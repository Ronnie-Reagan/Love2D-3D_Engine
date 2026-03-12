#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <objbase.h>
#include <wincodec.h>
#endif

namespace NativeGame {

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t version = 0;
};

#if defined(_WIN32)
namespace ImageCodecDetail {

template <typename T>
void safeRelease(T*& ptr)
{
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

struct ScopedComInit {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    bool usable() const
    {
        // WIC only needs COM to be available on the current thread.
        // If some other part of the app already initialized COM with a different
        // apartment type, CoInitializeEx returns RPC_E_CHANGED_MODE even though
        // COM is still usable on that thread.
        return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }

    ~ScopedComInit()
    {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

inline bool fillError(std::string* error, const char* message, HRESULT hr = S_OK)
{
    if (error != nullptr) {
        if (FAILED(hr)) {
            *error = std::string(message) + " (hr=0x" + [] (HRESULT value) {
                char buffer[16] {};
                std::snprintf(buffer, sizeof(buffer), "%08lx", static_cast<unsigned long>(value));
                return std::string(buffer);
            }(hr) + ")";
        } else {
            *error = message;
        }
    }
    return false;
}

}  // namespace ImageCodecDetail
#endif

inline bool decodeImageBytes(const std::vector<std::uint8_t>& bytes, RgbaImage& outImage, std::string* error)
{
    outImage = {};
    if (bytes.empty()) {
        if (error != nullptr) {
            *error = "image payload is empty";
        }
        return false;
    }

#if defined(_WIN32)
    using namespace ImageCodecDetail;
    ScopedComInit comInit;
    if (!comInit.usable()) {
        return fillError(error, "CoInitializeEx failed", comInit.hr);
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return fillError(error, "CoCreateInstance(CLSID_WICImagingFactory) failed", hr);
    }

    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        safeRelease(factory);
        return fillError(error, "IWICImagingFactory::CreateStream failed", hr);
    }

    hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes.data())),
        static_cast<DWORD>(bytes.size()));
    if (FAILED(hr)) {
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICStream::InitializeFromMemory failed", hr);
    }

    hr = factory->CreateDecoderFromStream(
        stream,
        nullptr,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(hr)) {
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICImagingFactory::CreateDecoderFromStream failed", hr);
    }

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        safeRelease(decoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapDecoder::GetFrame failed", hr);
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        safeRelease(frame);
        safeRelease(decoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapFrameDecode::GetSize failed", hr);
    }

    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        safeRelease(frame);
        safeRelease(decoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICImagingFactory::CreateFormatConverter failed", hr);
    }

    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        safeRelease(converter);
        safeRelease(frame);
        safeRelease(decoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICFormatConverter::Initialize failed", hr);
    }

    outImage.width = static_cast<int>(width);
    outImage.height = static_cast<int>(height);
    outImage.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    outImage.version = 1;
    hr = converter->CopyPixels(
        nullptr,
        width * 4u,
        static_cast<UINT>(outImage.pixels.size()),
        reinterpret_cast<BYTE*>(outImage.pixels.data()));

    safeRelease(converter);
    safeRelease(frame);
    safeRelease(decoder);
    safeRelease(stream);
    safeRelease(factory);

    if (FAILED(hr)) {
        outImage = {};
        return fillError(error, "IWICFormatConverter::CopyPixels failed", hr);
    }
    return true;
#else
    if (error != nullptr) {
        *error = "native image decode is only implemented on Windows";
    }
    return false;
#endif
}

inline bool loadImageFile(const std::filesystem::path& path, RgbaImage& outImage, std::string* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error != nullptr) {
            *error = "could not open image file";
        }
        return false;
    }

    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    return decodeImageBytes(bytes, outImage, error);
}

inline bool encodePngBytes(const RgbaImage& image, std::vector<std::uint8_t>& outBytes, std::string* error)
{
    outBytes.clear();
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u) {
        if (error != nullptr) {
            *error = "invalid RGBA image";
        }
        return false;
    }

#if defined(_WIN32)
    using namespace ImageCodecDetail;
    ScopedComInit comInit;
    if (!comInit.usable()) {
        return fillError(error, "CoInitializeEx failed", comInit.hr);
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* propertyBag = nullptr;
    IStream* stream = nullptr;
    HGLOBAL memoryHandle = nullptr;

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return fillError(error, "CoCreateInstance(CLSID_WICImagingFactory) failed", hr);
    }

    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr)) {
        safeRelease(factory);
        return fillError(error, "CreateStreamOnHGlobal failed", hr);
    }

    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICImagingFactory::CreateEncoder failed", hr);
    }

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapEncoder::Initialize failed", hr);
    }

    hr = encoder->CreateNewFrame(&frame, &propertyBag);
    if (FAILED(hr)) {
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapEncoder::CreateNewFrame failed", hr);
    }

    hr = frame->Initialize(propertyBag);
    if (FAILED(hr)) {
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapFrameEncode::Initialize failed", hr);
    }

    hr = frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height));
    if (FAILED(hr)) {
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapFrameEncode::SetSize failed", hr);
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapFrameEncode::SetPixelFormat failed", hr);
    }

    hr = frame->WritePixels(
        static_cast<UINT>(image.height),
        static_cast<UINT>(image.width * 4),
        static_cast<UINT>(image.pixels.size()),
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(image.pixels.data())));
    if (FAILED(hr)) {
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapFrameEncode::WritePixels failed", hr);
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapFrameEncode::Commit failed", hr);
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "IWICBitmapEncoder::Commit failed", hr);
    }

    hr = GetHGlobalFromStream(stream, &memoryHandle);
    if (FAILED(hr)) {
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "GetHGlobalFromStream failed", hr);
    }

    SIZE_T size = GlobalSize(memoryHandle);
    void* locked = GlobalLock(memoryHandle);
    if (locked == nullptr || size == 0) {
        if (locked != nullptr) {
            GlobalUnlock(memoryHandle);
        }
        safeRelease(propertyBag);
        safeRelease(frame);
        safeRelease(encoder);
        safeRelease(stream);
        safeRelease(factory);
        return fillError(error, "GlobalLock failed");
    }

    outBytes.resize(size);
    std::memcpy(outBytes.data(), locked, size);
    GlobalUnlock(memoryHandle);

    safeRelease(propertyBag);
    safeRelease(frame);
    safeRelease(encoder);
    safeRelease(stream);
    safeRelease(factory);
    return true;
#else
    if (error != nullptr) {
        *error = "native PNG encode is only implemented on Windows";
    }
    return false;
#endif
}

inline bool savePngFile(const std::filesystem::path& path, const RgbaImage& image, std::string* error)
{
    std::vector<std::uint8_t> bytes;
    if (!encodePngBytes(image, bytes, error)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (error != nullptr) {
            *error = "could not open PNG path for writing";
        }
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

}  // namespace NativeGame
