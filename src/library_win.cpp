#include "sampler_engine.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace samplermode {
namespace {
namespace fs = std::filesystem;

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseInteger(const std::string& text, int minimum, int maximum, int& result) {
    try {
        size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (consumed != text.size() || value < minimum || value > maximum)
            return false;
        result = value;
        return true;
    } catch (...) {
        return false;
    }
}

uint16_t read16(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
        (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t read32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

float readSample(const std::vector<uint8_t>& bytes, size_t offset, uint16_t format, uint16_t bits) {
    if (format == 3 && bits == 32) {
        const uint32_t bitsValue = read32(bytes, offset);
        float value = 0.0f;
        static_assert(sizeof(value) == sizeof(bitsValue));
        std::memcpy(&value, &bitsValue, sizeof(value));
        return std::isfinite(value) ? value : 0.0f;
    }
    if (format == 3 && bits == 64) {
        uint64_t bitsValue = 0;
        for (int index = 0; index < 8; ++index)
            bitsValue |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
        double value = 0.0;
        static_assert(sizeof(value) == sizeof(bitsValue));
        std::memcpy(&value, &bitsValue, sizeof(value));
        return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
    }
    if (format != 1)
        return 0.0f;
    switch (bits) {
    case 8:
        return (static_cast<int>(bytes[offset]) - 128) / 128.0f;
    case 16:
        return static_cast<int16_t>(read16(bytes, offset)) / 32768.0f;
    case 24: {
        int32_t value = static_cast<int32_t>(bytes[offset]) |
            (static_cast<int32_t>(bytes[offset + 1]) << 8) |
            (static_cast<int32_t>(bytes[offset + 2]) << 16);
        if (value & 0x00800000)
            value |= static_cast<int32_t>(0xff000000);
        return value / 8388608.0f;
    }
    case 32:
        return static_cast<int32_t>(read32(bytes, offset)) / 2147483648.0f;
    default:
        return 0.0f;
    }
}
}

bool loadWav(const std::wstring& path, SampleData& sample, std::wstring& error) {
    std::ifstream input(fs::path(path), std::ios::binary | std::ios::ate);
    if (!input) {
        error = L"Не удалось открыть WAV-файл.";
        return false;
    }
    const auto length = input.tellg();
    if (length < 12 || length > static_cast<std::streamoff>(2ull * 1024 * 1024 * 1024)) {
        error = L"Некорректный размер WAV-файла.";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!input || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = L"Файл не является RIFF/WAVE.";
        return false;
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t sampleRate = 0;
    size_t dataOffset = 0;
    size_t dataSize = 0;
    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const uint32_t chunkSize = read32(bytes, offset + 4);
        if (chunkSize > bytes.size() - offset - 8) {
            error = L"Повреждён блок данных WAV.";
            return false;
        }
        const size_t body = offset + 8;
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && chunkSize >= 16) {
            format = read16(bytes, body);
            channels = read16(bytes, body + 2);
            sampleRate = read32(bytes, body + 4);
            bits = read16(bytes, body + 14);
            if (format == 0xfffe && chunkSize >= 40)
                format = read16(bytes, body + 24);
        } else if (std::memcmp(bytes.data() + offset, "data", 4) == 0) {
            dataOffset = body;
            dataSize = chunkSize;
        }
        offset = body + chunkSize + (chunkSize & 1u);
    }

    const bool supported = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
        (format == 3 && (bits == 32 || bits == 64));
    if (!supported || channels == 0 || sampleRate == 0 || dataOffset == 0 || dataSize == 0) {
        error = L"Поддерживаются WAV PCM 8/16/24/32 бит и float 32/64 бит.";
        return false;
    }
    const size_t bytesPerSample = bits / 8;
    const size_t frameSize = bytesPerSample * channels;
    const size_t frameCount = dataSize / frameSize;
    const uint16_t outputChannels = channels == 1 ? 1 : 2;
    sample = SampleData {};
    sample.name = wideToUtf8(fs::path(path).filename().wstring());
    sample.sampleRate = sampleRate;
    sample.channels = outputChannels;
    sample.frames.resize(frameCount * outputChannels);
    for (size_t frame = 0; frame < frameCount; ++frame) {
        for (uint16_t channel = 0; channel < outputChannels; ++channel) {
            sample.frames[frame * outputChannels + channel] =
                readSample(bytes, dataOffset + (frame * channels + channel) * bytesPerSample,
                           format, bits);
        }
    }
    return true;
}

std::vector<std::wstring> listConfigurations(const std::wstring& root) {
    std::vector<std::wstring> names;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(fs::path(root), error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == L".txt")
            names.push_back(entry.path().filename().wstring());
    }
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    return names;
}

std::shared_ptr<SampleLibrary> loadLibrary(const std::wstring& root,
                                           const std::wstring& configName,
                                           std::wstring& error) {
    error.clear();
    const fs::path configPath = fs::path(root) / configName;
    std::ifstream input(configPath, std::ios::binary);
    if (!input) {
        error = L"Не удалось открыть файл конфигурации.";
        return {};
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xef &&
        static_cast<uint8_t>(text[1]) == 0xbb && static_cast<uint8_t>(text[2]) == 0xbf)
        text.erase(0, 3);

    std::string normalized;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        normalized += line;
        normalized += ' ';
    }

    const std::regex entryPattern(
        R"(File\s+name\s*:\s*([^=]+?)\s*=\s*(.*?)(?=File\s+name\s*:|$))",
        std::regex::icase);
    auto library = std::make_shared<SampleLibrary>();
    library->root = root;
    library->configName = configName;
    int entryNumber = 0;
    for (std::sregex_iterator it(normalized.begin(), normalized.end(), entryPattern), end;
         it != end; ++it) {
        ++entryNumber;
        const std::string fileName = trim((*it)[1].str());
        const std::string options = (*it)[2].str();
        SampleMapping mapping;
        bool valid = true;
        std::wstring problem;
        std::istringstream tokens(options);
        std::string token;
        while (tokens >> token) {
            if (token.rfind("but", 0) == 0) {
                int value = 0;
                valid = parseInteger(token.substr(3), 0, 1, value);
                mapping.onRelease = value == 1;
            } else if (token.rfind("not", 0) == 0) {
                valid = parseInteger(token.substr(3), 1, 127, mapping.note);
            } else if (token.rfind("vel", 0) == 0) {
                const std::string range = token.substr(3);
                const size_t dash = range.find('-');
                valid = dash != std::string::npos &&
                    parseInteger(range.substr(0, dash), 0, 127, mapping.velocityMin) &&
                    parseInteger(range.substr(dash + 1), 0, 127, mapping.velocityMax) &&
                    mapping.velocityMin <= mapping.velocityMax;
            } else if (token.rfind("vol", 0) == 0) {
                std::string value = token.substr(3);
                if (value.size() > 2 && value.substr(value.size() - 2) == "db")
                    value.erase(value.size() - 2);
                try {
                    size_t consumed = 0;
                    mapping.gain = std::stof(value, &consumed);
                    valid = consumed == value.size() && mapping.gain >= -140.0f && mapping.gain <= 12.0f;
                } catch (...) {
                    valid = false;
                }
            } else if (token.rfind("mut", 0) == 0) {
                valid = parseInteger(token.substr(3), 0, 127, mapping.muteNote);
            } else if (token.rfind("out", 0) == 0) {
                const std::string range = token.substr(3);
                const size_t slash = range.find('/');
                int first = 0;
                int second = 0;
                valid = slash != std::string::npos &&
                    parseInteger(range.substr(0, slash), 1, kOutputChannels, first) &&
                    parseInteger(range.substr(slash + 1), 1, kOutputChannels, second) &&
                    first % 2 == 1 && second == first + 1;
                mapping.outputOffset = first - 1;
            } else {
                library->warnings.push_back(L"Неизвестный параметр " + utf8ToWide(token) +
                                            L" в строке " + std::to_wstring(entryNumber) + L".");
            }
            if (!valid) {
                problem = L"Некорректный параметр " + utf8ToWide(token) + L".";
                break;
            }
        }
        if (!valid) {
            library->warnings.push_back(L"Строка " + std::to_wstring(entryNumber) + L" пропущена: " + problem);
            continue;
        }
        if (fileName.empty()) {
            library->warnings.push_back(L"Строка " + std::to_wstring(entryNumber) + L": пустое имя файла.");
            continue;
        }
        const fs::path relative = fs::path(utf8ToWide(fileName));
        if (relative.empty() || relative.is_absolute() || relative.has_root_name()) {
            library->warnings.push_back(L"Строка " + std::to_wstring(entryNumber) + L": недопустимый путь.");
            continue;
        }
        bool hasParent = false;
        for (const auto& part : relative) {
            if (part == L"..")
                hasParent = true;
        }
        if (hasParent) {
            library->warnings.push_back(L"Строка " + std::to_wstring(entryNumber) + L": путь вне Samples запрещён.");
            continue;
        }
        auto sample = std::make_shared<SampleData>();
        std::wstring wavError;
        if (!loadWav((fs::path(root) / L"Samples" / relative).wstring(), *sample, wavError)) {
            library->warnings.push_back(utf8ToWide(fileName) + L": " + wavError);
            continue;
        }
        mapping.sample = std::move(sample);
        library->mappings.push_back(std::move(mapping));
    }

    if (library->mappings.empty()) {
        error = L"Не загружено ни одного семпла. Проверьте конфигурацию и папку Samples.";
        if (!library->warnings.empty())
            error += L" " + library->warnings.front();
        return {};
    }
    return library;
}

} // namespace samplermode
