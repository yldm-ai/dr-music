#include "cover.h"
#include <fileref.h>
#include <tvariant.h>
#include <wincodec.h>
#include <wrl.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <vector>
namespace dr {
namespace {
using Microsoft::WRL::ComPtr;
constexpr size_t max_bytes = 64 * 1024 * 1024;
constexpr unsigned max_edge = 1024;
constexpr unsigned long long max_pixels = 64ULL * 1024 * 1024;
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
bool dimensions(unsigned width, unsigned height) {
    return width && height && width <= 32768 && height <= 32768 &&
        static_cast<unsigned long long>(width) * height <= max_pixels;
}
unsigned little(std::string_view bytes, size_t start, unsigned count) {
    unsigned value = 0;
    for (unsigned i = 0; i < count; ++i) value |= static_cast<unsigned char>(bytes[start + i]) << (i * 8);
    return value;
}
// Windows installations without a WebP WIC codec can still serve a bounded
// WebP to WebView2. Verify its container and image header before accepting it.
std::optional<CoverImage> webp(std::string_view bytes, const std::string &source) {
    if (bytes.size() < 20 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "WEBP" ||
        static_cast<size_t>(little(bytes, 4, 4)) + 8 != bytes.size()) return {};
    for (size_t offset = 12; offset + 8 <= bytes.size();) {
        const auto kind = bytes.substr(offset, 4);
        const auto size = little(bytes, offset + 4, 4);
        const auto data = offset + 8;
        if (size > bytes.size() - data) return {};
        unsigned width = 0, height = 0;
        if (kind == "VP8X" && size >= 10) {
            width = 1 + little(bytes, data + 4, 3); height = 1 + little(bytes, data + 7, 3);
        } else if (kind == "VP8 " && size >= 10 && bytes.substr(data + 3, 3) == std::string_view("\x9d\x01\x2a", 3)) {
            width = little(bytes, data + 6, 2) & 0x3fff; height = little(bytes, data + 8, 2) & 0x3fff;
        } else if (kind == "VP8L" && size >= 5 && static_cast<unsigned char>(bytes[data]) == 0x2f) {
            const auto bits = little(bytes, data + 1, 4);
            width = (bits & 0x3fff) + 1; height = ((bits >> 14) & 0x3fff) + 1;
        }
        if (dimensions(width, height)) return CoverImage{std::string(bytes), "image/webp", source, width, height};
        offset = data + size + (size & 1);
    }
    return {};
}
int priority(const TagLib::VariantMap &picture) {
    auto type = lower(picture.value("pictureType").toString().to8Bit(true));
    if (type == "front cover" || type == "cover (front)") return 0;
    if (type == "back cover" || type == "cover (back)") return 2;
    if (type.find("icon") != std::string::npos) return 3;
    return 1;
}
}
std::optional<CoverImage> decode_cover(std::string_view bytes, const std::string &source) {
    if (bytes.empty() || bytes.size() > max_bytes) return {};
    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return {};
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return {};
    ComPtr<IWICStream> input;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateStream(&input)) ||
        FAILED(input->InitializeFromMemory(reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data())), static_cast<DWORD>(bytes.size())))) return {};
    const auto decoded = factory->CreateDecoderFromStream(input.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(decoded)) return decoded == WINCODEC_ERR_COMPONENTNOTFOUND ? webp(bytes, source) : std::nullopt;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT width = 0, height = 0;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || !dimensions(width, height)) return {};
    ComPtr<IWICBitmapSource> image;
    if (FAILED(frame.As(&image))) return {};
    if (std::max(width, height) > max_edge) {
        const auto edge = std::max(width, height);
        width = std::max(1U, static_cast<unsigned>((static_cast<unsigned long long>(width) * max_edge) / edge));
        height = std::max(1U, static_cast<unsigned>((static_cast<unsigned long long>(height) * max_edge) / edge));
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(image.Get(), width, height, WICBitmapInterpolationModeFant)) ||
            FAILED(scaler.As(&image))) return {};
    }
    ComPtr<IStream> output;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> encoded;
    ComPtr<IPropertyBag2> options;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &output)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(output.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&encoded, &options)) ||
        FAILED(encoded->Initialize(options.Get())) || FAILED(encoded->SetSize(width, height))) return {};
    auto format = GUID_WICPixelFormat32bppBGRA;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(encoded->SetPixelFormat(&format)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(image.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(encoded->WriteSource(converter.Get(), nullptr)) || FAILED(encoded->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stat{};
    if (FAILED(output->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > max_bytes) return {};
    std::string png(static_cast<size_t>(stat.cbSize.QuadPart), '\0');
    LARGE_INTEGER start{}; ULONG read = 0;
    if (FAILED(output->Seek(start, STREAM_SEEK_SET, nullptr)) ||
        FAILED(output->Read(png.data(), static_cast<ULONG>(png.size()), &read)) || read != png.size()) return {};
    return CoverImage{std::move(png), "image/png", source, width, height};
}
CoverInputs CoverCache::inputs(const fs::path &audio) {
    CoverInputs result;
    const auto directory_key = stored_path_key(audio.parent_path());
    auto directory = directories_.get(directory_key);
    if (!directory) {
        auto found = std::make_shared<Directory>();
        WIN32_FIND_DATAW data{};
        struct Search { HANDLE handle; ~Search() { if (handle != INVALID_HANDLE_VALUE) FindClose(handle); } };
        Search search{FindFirstFileW((audio.parent_path() / L"*").c_str(), &data)};
        if (search.handle == INVALID_HANDLE_VALUE) {
            result.valid = GetLastError() == ERROR_FILE_NOT_FOUND;
        } else {
            do {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const fs::path name(data.cFileName);
                const auto ext = lower(path_text(name.extension()));
                if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".webp" &&
                    ext != ".bmp" && ext != ".gif" && ext != ".tif" && ext != ".tiff") continue;
                auto time = [](FILETIME value) { return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
                const auto size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
                const auto stamp = std::to_string(size) + ":" + std::to_string(time(data.ftLastWriteTime)) + ":" + std::to_string(time(data.ftCreationTime));
                found->emplace(stored_path_key(name), CoverFile{audio.parent_path() / name, stamp});
            } while (FindNextFileW(search.handle, &data));
            result.valid = GetLastError() == ERROR_NO_MORE_FILES;
        }
        directory = found;
        if (result.valid) {
            size_t bytes = 0;
            for (const auto &[name, file] : *found) bytes += 128 + name.size() + file.path.native().size() * sizeof(wchar_t) + file.stamp.size();
            directories_.put(directory_key, found, bytes);
        }
    }
    // A directory is enumerated once per scan, including missing candidates.
    // Editing, adding or deleting any candidate is observed on the next scan.
    Json fingerprint = Json::array();
    const std::array<fs::path, 5> names{audio.stem(), L"cover", L"folder", L"front", L"album"};
    for (const auto &name : names) for (const auto *extension : {L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".gif", L".tif", L".tiff"}) {
        auto candidate = name; candidate += extension;
        const auto found = directory->find(stored_path_key(candidate));
        if (found == directory->end()) continue;
        result.files.push_back(found->second);
        fingerprint.push_back({path_text(found->second.path.filename()), found->second.stamp});
    }
    result.fingerprint = content_hash(fingerprint.dump());
    return result;
}
std::shared_ptr<const CoverImage> CoverCache::decode(std::string_view bytes) {
    if (bytes.empty() || bytes.size() > max_bytes) return {};
    const auto key = content_hash(bytes);
    auto image = images_.get(key);
    if (!image) {
        // Empty images are bounded negative entries: a corrupt APIC repeated
        // across an album should not repeatedly invoke the image decoder.
        image = std::make_shared<CoverImage>(decode_cover(bytes).value_or(CoverImage{}));
        images_.put(key, image, image->bytes.size() + 128);
    }
    return image->bytes.empty() ? nullptr : image;
}
CoverResult CoverCache::find(const CoverInputs &inputs, const TagLib::FileRef *tags) {
    CoverResult result;
    bool rejected = !inputs.valid;
    if (tags) {
        try {
            const auto pictures = tags->complexProperties("PICTURE");
            std::vector<TagLib::VariantMap> candidates(pictures.begin(), pictures.end());
            std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return priority(a) < priority(b); });
            for (const auto &picture : candidates) {
                const auto data = picture.value("data").toByteVector();
                result.image = decode(std::string_view(data.data(), data.size()));
                if (result.image) { result.source = "embedded"; return result; }
                rejected = true;
            }
        } catch (const std::exception &) { rejected = true; }
    }
    // Restrict fallback to known names in the song's own directory. Never
    // search recursively, navigate to a parent, or use tag text as a path.
    for (const auto &candidate : inputs.files) {
        const auto &file = candidate.path;
        const auto key = stored_path_key(file) + ":" + candidate.stamp;
        auto bytes = files_.get(key);
        if (!bytes) {
            std::error_code error;
            const auto size = fs::file_size(file, error);
            if (error || !size || size > max_bytes || file_stamp(file) != candidate.stamp) { rejected = true; continue; }
            std::ifstream input(file, std::ios::binary);
            auto read = std::make_shared<std::string>(static_cast<size_t>(size), '\0');
            if (!input.read(read->data(), static_cast<std::streamsize>(size)) || file_stamp(file) != candidate.stamp) { rejected = true; continue; }
            bytes = read; files_.put(key, bytes, bytes->size());
        }
        result.image = decode(*bytes);
        if (result.image) { result.source = path_text(file.filename()); return result; }
        rejected = true;
    }
    if (rejected) result.error = "封面候选图片损坏、格式不支持或超过读取上限";
    return result;
}
}
