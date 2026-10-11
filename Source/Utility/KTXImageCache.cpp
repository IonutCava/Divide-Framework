#include "Headers/KTXImageCache.h"
#include "EngineConfig.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <system_error>
#include <type_traits>

#include <png.h>
#include <jpeglib.h>
#include <SDL3/SDL_version.h>

namespace Divide::ImageTools
{
    namespace
    {
        constexpr size_t SOURCE_LIMIT = 32u * 1024u * 1024u;
        constexpr size_t DECODED_LIMIT = 32u * 1024u * 1024u;
        constexpr size_t PAYLOAD_LIMIT = 64u * 1024u * 1024u;
        constexpr std::string_view PREFIX = "divide-ktx-source-v1-";
        constexpr std::array<char, 8> MAGIC{'D', 'V', 'K', 'T', 'X', 'C', '0', '1'};
        // Increment these when decoder, filtering, or native encoding changes.
        constexpr U32 SCHEMA_VERSION = 1u;
        constexpr U32 SOURCE_VERSION = 2u;
        constexpr U32 MIP_VERSION = 3u;
        constexpr U32 ENCODER_VERSION = 1u;
        constexpr U32 PRECISION_POLICY_VERSION = 2u;
        constexpr U32 CHANNEL_POLICY_VERSION = 2u;

        struct Flight
        {
            std::shared_ptr<const std::vector<Byte>> _source;
            std::vector<Byte> _settings;
            std::vector<Byte> _payload;
            std::string _error = "KTX source conversion failed or was cancelled";
            std::condition_variable _changed;
            bool _done = false;
            bool _success = false;
        };

        std::mutex g_mutex;
        std::mutex g_diskMutex;
        std::condition_variable g_slotsChanged;
        size_t g_active = 0u;
        std::map<U64, std::shared_ptr<Flight>> g_flights;
        std::atomic<U64> g_tempSequence{0u};

        bool Fail(std::string* error, const char* message)
        {
            if (error != nullptr) { *error = message; }
            return false;
        }

        struct Slot
        {
            bool _held = false;
            bool acquire(const std::stop_token token)
            {
                std::unique_lock lock(g_mutex);
                while (g_active >= 2u && !token.stop_requested())
                {
                    g_slotsChanged.wait_for(lock, std::chrono::milliseconds(25));
                }
                if (token.stop_requested()) { return false; }
                ++g_active;
                _held = true;
                return true;
            }
            ~Slot()
            {
                if (_held)
                {
                    std::lock_guard lock(g_mutex);
                    --g_active;
                    g_slotsChanged.notify_all();
                }
            }
        };

        void Append(std::vector<Byte>& bytes, const U64 value, const size_t count)
        {
            for (size_t i = 0u; i < count; ++i)
            {
                bytes.push_back(static_cast<Byte>((value >> (i * 8u)) & 255u));
            }
        }

        U64 Digest(const std::span<const Byte> bytes, const U64 seed = val_64_const)
        {
            // Engine _ID_VIEW recurses per byte; use its FNV constants iteratively
            // for large binary input. This is an index, not an authenticity check.
            U64 hash = seed;
            for (const Byte byte : bytes)
            {
                hash = (hash ^ static_cast<U8>(byte)) * prime_64_const;
            }
            return hash;
        }

        std::vector<Byte> CanonicalSettings(const KTXSourceSettings& settings)
        {
            std::vector<Byte> bytes;
            for (const U32 version : {SCHEMA_VERSION, SOURCE_VERSION, MIP_VERSION, ENCODER_VERSION,
                                      PRECISION_POLICY_VERSION, CHANNEL_POLICY_VERSION})
            {
                Append(bytes, version, 4u);
            }
            Append(bytes, static_cast<U8>(settings._format), 1u);
            Append(bytes, settings._srgb, 1u);
            Append(bytes, static_cast<U8>(settings._alphaInterpretation), 1u);
            Append(bytes, static_cast<U8>(settings._semantic), 1u);
            Append(bytes, settings._generateMipmaps, 1u);
            Append(bytes, settings._preserveAlphaCoverage, 1u);
            Append(bytes, std::bit_cast<U32>(Config::ALPHA_DISCARD_THRESHOLD), 4u);
            Append(bytes, PNG_LIBPNG_VER, 4u);
            Append(bytes, JPEG_LIB_VERSION, 4u);
            Append(bytes, SDL_VERSION, 4u);
#ifdef LIBJPEG_TURBO_VERSION_NUMBER
            Append(bytes, LIBJPEG_TURBO_VERSION_NUMBER, 4u);
#else
            Append(bytes, 0u, 4u);
#endif
            // Matches the Ktx version required by ImportPackages.cmake.
            Append(bytes, 40402u, 4u);
            return bytes;
        }

        bool ReadSource(const std::filesystem::path& path, std::vector<Byte>& bytes)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) { return false; }
            const auto length = stream.tellg();
            if (length <= 0 || length > static_cast<std::streamoff>(SOURCE_LIMIT)) { return false; }
            bytes.resize(static_cast<size_t>(length));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return stream.good() && stream.peek() == std::char_traits<char>::eof();
        }

        bool ReadNumber(std::istream& stream, U64& value)
        {
            std::array<unsigned char, 8> bytes{};
            stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            if (!stream) { return false; }
            value = 0u;
            for (size_t i = 0; i < bytes.size(); ++i) { value |= static_cast<U64>(bytes[i]) << (8u * i); }
            return true;
        }

        void WriteNumber(std::ostream& stream, const U64 value)
        {
            std::array<unsigned char, 8> bytes{};
            for (size_t i = 0; i < bytes.size(); ++i) { bytes[i] = static_cast<unsigned char>(value >> (8u * i)); }
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        bool CompareBytes(std::istream& stream, const std::span<const Byte> expected)
        {
            std::array<Byte, 8192> buffer{};
            for (size_t offset = 0u; offset < expected.size();)
            {
                const size_t count = std::min(buffer.size(), expected.size() - offset);
                stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
                if (!stream || !std::equal(buffer.begin(), buffer.begin() + count, expected.begin() + offset))
                {
                    return false;
                }
                offset += count;
            }
            return true;
        }

        bool ReadEntry(const std::filesystem::path& path, const Flight& flight, std::vector<Byte>& payload)
        {
            std::error_code ec;
            const auto length = std::filesystem::file_size(path, ec);
            if (ec || length > KTX_IMAGE_BYTE_LIMIT) { return false; }
            std::ifstream stream(path, std::ios::binary);
            std::array<char, 8> magic{};
            stream.read(magic.data(), magic.size());
            U64 settingsSize = 0u, sourceSize = 0u, payloadSize = 0u, digest = 0u;
            if (magic != MAGIC || !ReadNumber(stream, settingsSize) || !ReadNumber(stream, sourceSize) ||
                !ReadNumber(stream, payloadSize) || !ReadNumber(stream, digest) ||
                settingsSize != flight._settings.size() || sourceSize != flight._source->size() ||
                payloadSize == 0u || payloadSize > PAYLOAD_LIMIT ||
                length != MAGIC.size() + 32u + settingsSize + sourceSize + payloadSize ||
                !CompareBytes(stream, flight._settings) || !CompareBytes(stream, *flight._source))
            {
                return false;
            }
            payload.resize(static_cast<size_t>(payloadSize));
            stream.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
            if (!stream || Digest(payload) != digest) { payload.clear(); return false; }
            return true;
        }

        bool LoadPayload(const std::span<const Byte> payload, KTXImage& image, std::string* error)
        {
            // Bound the declared native mip bytes before libktx allocates. The
            // native loader then verifies these declarations against dimensions,
            // formats, topology, and actual level ranges.
            if (payload.size() < 80u || payload.size() > PAYLOAD_LIMIT)
            {
                return Fail(error, "Invalid cached KTX payload size");
            }
            const auto number = [&](const size_t offset, const size_t count)
            {
                U64 value = 0u;
                for (size_t i = 0u; i < count; ++i)
                {
                    value |= static_cast<U64>(static_cast<U8>(payload[offset + i])) << (8u * i);
                }
                return value;
            };
            const U64 levels = number(40u, 4u);
            if (levels == 0u || levels > 16u || 80u + 24u * levels > payload.size())
            {
                return Fail(error, "Invalid cached KTX mip count");
            }
            U64 decoded = 0u;
            for (size_t i = 0u; i < levels; ++i)
            {
                const U64 bytes = number(80u + i * 24u + 16u, 8u);
                if (bytes > DECODED_LIMIT - decoded)
                {
                    return Fail(error, "Cached KTX mip bytes exceed 32 MiB");
                }
                decoded += bytes;
            }
            return LoadKTXImage(payload, image, error, DECODED_LIMIT);
        }

        bool FitsMipBudget(const KTXImage& image, const bool generate)
        {
            size_t bytes = 0u;
            for (const KTXSubresource& slice : image._subresources)
            {
                if (generate && slice._mip != 0u) { continue; }
                const size_t sampleBytes = std::visit([](const auto& pixels)
                {
                    return sizeof(typename std::decay_t<decltype(pixels)>::value_type);
                }, slice._pixels);
                size_t width = slice._width, height = slice._height;
                do
                {
                    const size_t level = width * height * image._components * sampleBytes;
                    if (level > DECODED_LIMIT - bytes) { return false; }
                    bytes += level;
                    if (!generate || (width == 1u && height == 1u)) { break; }
                    width = std::max(size_t{1u}, width / 2u);
                    height = std::max(size_t{1u}, height / 2u);
                } while (true);
            }
            return true;
        }

        bool OwnedEntry(const std::filesystem::path& path)
        {
            const std::string name = path.filename().string();
            if (!name.starts_with(PREFIX) || path.extension() != ".entry") { return false; }
            const auto key = std::string_view(name).substr(PREFIX.size(), name.size() - PREFIX.size() - 6u);
            return !key.empty() && key.size() <= 20u &&
                   std::all_of(key.begin(), key.end(), [](const char c) { return c >= '0' && c <= '9'; });
        }

        bool Evict(const std::filesystem::path& directory, const U64 quota, const U64 incoming)
        {
            struct Entry { std::filesystem::path _path; std::filesystem::file_time_type _time; U64 _size; };
            std::vector<Entry> entries;
            U64 total = incoming;
            std::error_code ec;
            for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
            {
                if (!OwnedEntry(it->path()) || !it->is_regular_file(ec)) { ec.clear(); continue; }
                const U64 size = it->file_size(ec);
                if (ec) { ec.clear(); continue; }
                const auto time = it->last_write_time(ec);
                if (ec) { ec.clear(); continue; }
                total = size > UINT64_MAX - total ? UINT64_MAX : total + size;
                entries.push_back({it->path(), time, size});
            }
            if (ec) { return false; }
            std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a._time < b._time; });
            for (const Entry& entry : entries)
            {
                if (total <= quota) { break; }
                if (std::filesystem::remove(entry._path, ec)) { total -= entry._size; }
                ec.clear();
            }
            return total <= quota;
        }

        struct TemporaryFile
        {
            std::filesystem::path _path;
            std::filesystem::path _directory;
            ~TemporaryFile()
            {
                if (!_path.empty()) { std::error_code ec; std::filesystem::remove(_path, ec); }
                if (!_directory.empty()) { std::error_code ec; std::filesystem::remove(_directory, ec); }
            }
        };

        bool Publish(const std::filesystem::path& directory, const std::filesystem::path& path,
                     const std::filesystem::path& source, const Flight& flight,
                     const U64 quota, const std::stop_token token)
        {
            const U64 length = MAGIC.size() + 32u + flight._settings.size() +
                               flight._source->size() + flight._payload.size();
            if (length > quota || length > KTX_IMAGE_BYTE_LIMIT || token.stop_requested()) { return true; }
            std::lock_guard diskLock(g_diskMutex);
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            if (ec) { return true; }
            // Randomness separates processes; the counter separates calls.
            const U64 nonce = (static_cast<U64>(std::random_device{}()) << 32u) ^ std::random_device{}();
            auto temporaryDirectory = path;
            temporaryDirectory += "." + std::to_string(nonce) + "." +
                                  std::to_string(g_tempSequence.fetch_add(1u)) + ".part";
            // Exclusive directory creation reserves a native-path temporary file
            // across processes without a check-then-open race.
            if (!std::filesystem::create_directory(temporaryDirectory, ec)) { return true; }
            TemporaryFile temporary{temporaryDirectory / "entry", temporaryDirectory};
            std::ofstream stream(temporary._path, std::ios::binary | std::ios::trunc);
            if (!stream) { return true; }
            stream.write(MAGIC.data(), MAGIC.size());
            WriteNumber(stream, flight._settings.size());
            WriteNumber(stream, flight._source->size());
            WriteNumber(stream, flight._payload.size());
            WriteNumber(stream, Digest(flight._payload));
            for (const auto* bytes : {&flight._settings, flight._source.get(), &flight._payload})
            {
                stream.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
            }
            stream.flush();
            if (!stream) { return true; }
            stream.close();
            if (!stream || token.stop_requested()) { return true; }
            if (!Evict(directory, quota, length)) { return true; }
            if (token.stop_requested()) { return true; }
            std::vector<Byte> current;
            if (!ReadSource(source, current) || current != *flight._source) { return false; }
            std::filesystem::rename(temporary._path, path, ec);
#if defined(_WIN32)
            // Windows rename does not replace an existing destination. A miss
            // during replacement is safe; readers never observe a partial entry.
            if (ec)
            {
                std::filesystem::remove(path, ec);
                if (!ec) { std::filesystem::rename(temporary._path, path, ec); }
            }
#endif
            if (!ec) { temporary._path.clear(); }
            return true;
        }

        struct FinishFlight
        {
            U64 _key;
            std::shared_ptr<Flight> _flight;
            ~FinishFlight()
            {
                if (!_flight) { return; }
                std::lock_guard lock(g_mutex);
                _flight->_done = true;
                const auto it = g_flights.find(_key);
                if (it != g_flights.end() && it->second == _flight) { g_flights.erase(it); }
                _flight->_changed.notify_all();
            }
        };
    }

    bool LoadCachedKTXSource(const ResourcePath& source, const ResourcePath& cacheDir,
                             const KTXSourceSettings& settings, KTXImage& output, std::string* error)
    {
        if (error != nullptr) { error->clear(); }
        Slot slot;
        if (!slot.acquire(settings._stopToken)) { return Fail(error, "KTX source conversion cancelled"); }
        try
        {
            if ((settings._format != KTXSourceFormat::PNG && settings._format != KTXSourceFormat::JPEG &&
                 settings._format != KTXSourceFormat::BMP) ||
                static_cast<U8>(settings._alphaInterpretation) > static_cast<U8>(KTXAlphaInterpretation::SYNTHETIC) ||
                static_cast<U8>(settings._semantic) > static_cast<U8>(KTXMipSemantic::DATA) ||
                (settings._srgb && settings._semantic != KTXMipSemantic::COLOR) ||
                (settings._preserveAlphaCoverage &&
                 (!settings._generateMipmaps || settings._semantic == KTXMipSemantic::DATA)))
            {
                return Fail(error, "Invalid KTX source semantic, colour space, or alpha coverage settings");
            }
            auto sourceBytes = std::make_shared<std::vector<Byte>>();
            if (!ReadSource(source.fileSystemPath(), *sourceBytes))
            {
                return Fail(error, "Unable to read KTX source (empty, changed, or exceeds 32 MiB)");
            }
            auto canonical = CanonicalSettings(settings);
            const U64 key = Digest(*sourceBytes, Digest(canonical));
            std::shared_ptr<Flight> flight;
            bool owner = false;
            {
                std::unique_lock lock(g_mutex);
                const auto it = g_flights.find(key);
                if (it != g_flights.end())
                {
                    if (it->second->_settings != canonical || *it->second->_source != *sourceBytes)
                    {
                        return Fail(error, "KTX in-flight cache key collision");
                    }
                    flight = it->second;
                    sourceBytes.reset();
                    while (!flight->_done && !settings._stopToken.stop_requested())
                    {
                        flight->_changed.wait_for(lock, std::chrono::milliseconds(25));
                    }
                }
                else
                {
                    flight = std::make_shared<Flight>();
                    flight->_source = std::move(sourceBytes);
                    flight->_settings = std::move(canonical);
                    g_flights.emplace(key, flight);
                    owner = true;
                }
            }
            FinishFlight finish{key, owner ? flight : nullptr};
            if (settings._stopToken.stop_requested()) { return Fail(error, "KTX source conversion cancelled"); }
            if (!owner)
            {
                if (!flight->_success)
                {
                    if (error != nullptr)
                    {
                        *error = flight->_error.empty() ? "KTX source conversion failed or was cancelled" : flight->_error;
                    }
                    return false;
                }
                KTXImage result;
                if (!LoadPayload(flight->_payload, result, error)) { return false; }
                std::vector<Byte> current;
                if (!ReadSource(source.fileSystemPath(), current) || current != *flight->_source)
                {
                    return Fail(error, "KTX source changed while waiting for conversion");
                }
                if (settings._stopToken.stop_requested()) { return Fail(error, "KTX source conversion cancelled"); }
                output = std::move(result);
                return true;
            }
            const auto path = cacheDir.fileSystemPath() / (std::string(PREFIX) + std::to_string(key) + ".entry");
            KTXImage result;
            bool hit = false;
            if (settings._diskCache)
            {
                try
                {
                    hit = ReadEntry(path, *flight, flight->_payload) &&
                          LoadPayload(flight->_payload, result, nullptr);
                }
                catch (const std::exception&) {}
            }
            if (!hit)
            {
                std::vector<Byte>().swap(flight->_payload);
                flight->_error.clear();
                if (!DecodeKTXSource(*flight->_source, settings._format, settings._srgb,
                                     settings._alphaInterpretation, result, &flight->_error, DECODED_LIMIT) ||
                    !FitsMipBudget(result, settings._generateMipmaps))
                {
                    if (flight->_error.empty()) { flight->_error = "KTX decoded mip chain exceeds 32 MiB"; }
                    if (error != nullptr) { *error = flight->_error; }
                    return false;
                }
                if (settings._stopToken.stop_requested()) { return Fail(error, "KTX source conversion cancelled"); }
                if (settings._generateMipmaps)
                {
                    KTXMipOptions options;
                    options._semantic = settings._semantic;
                    options._preserveAlphaCoverage = settings._preserveAlphaCoverage &&
                        result._components == 4u &&
                        result._alphaInterpretation == KTXAlphaInterpretation::OPACITY;
                    if (!GenerateKTXMipmaps(result, options, &flight->_error))
                    {
                        if (error != nullptr) { *error = flight->_error; }
                        return false;
                    }
                }
                if (settings._stopToken.stop_requested()) { return Fail(error, "KTX source conversion cancelled"); }
                if (!EncodeKTXImage(result, flight->_payload, &flight->_error) ||
                    flight->_payload.size() > PAYLOAD_LIMIT)
                {
                    if (error != nullptr) { *error = flight->_error.empty() ? "KTX payload exceeds 64 MiB" : flight->_error; }
                    return false;
                }
                result = {};
                if (!LoadPayload(flight->_payload, result, &flight->_error))
                {
                    if (error != nullptr) { *error = flight->_error; }
                    return false;
                }
            }
            std::vector<Byte> current;
            if (!ReadSource(source.fileSystemPath(), current) || current != *flight->_source)
            {
                flight->_error = "KTX source changed during conversion";
                if (error != nullptr) { *error = flight->_error; }
                return false;
            }
            std::vector<Byte>().swap(current);
            if (settings._stopToken.stop_requested()) { return Fail(error, "KTX source conversion cancelled"); }
            if (!hit && settings._diskCache)
            {
                // Cache storage is optional, including read-only and full disks.
                try
                {
                    if (!Publish(cacheDir.fileSystemPath(), path, source.fileSystemPath(), *flight,
                                 settings._cacheQuotaBytes, settings._stopToken))
                    {
                        flight->_error = "KTX source changed before cache publication";
                        if (error != nullptr) { *error = flight->_error; }
                        return false;
                    }
                }
                catch (const std::exception&) {}
            }
            if (settings._stopToken.stop_requested()) { return Fail(error, "KTX source conversion cancelled"); }
            flight->_success = true;
            flight->_error.clear();
            output = std::move(result);
            return true;
        }
        catch (const std::exception& exception)
        {
            if (error != nullptr) { *error = exception.what(); }
            return false;
        }
    }

    bool InvalidateKTXSourceCache(const ResourcePath& cacheDir, std::string* error)
    {
        if (error != nullptr) { error->clear(); }
        try
        {
            std::lock_guard diskLock(g_diskMutex);
            std::error_code ec;
            if (!std::filesystem::exists(cacheDir.fileSystemPath(), ec)) { return !ec; }
            for (std::filesystem::directory_iterator it(cacheDir.fileSystemPath(), ec), end;
                 !ec && it != end; it.increment(ec))
            {
                if (OwnedEntry(it->path()) && it->is_regular_file(ec))
                {
                    std::filesystem::remove(it->path(), ec);
                }
            }
            if (ec)
            {
                if (error != nullptr) { *error = ec.message(); }
                return false;
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            if (error != nullptr) { *error = exception.what(); }
            return false;
        }
    }
}
