#include "sampler_engine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void write16(std::ofstream& output, uint16_t value) {
    const char bytes[] = {static_cast<char>(value & 0xff), static_cast<char>(value >> 8)};
    output.write(bytes, 2);
}

void write32(std::ofstream& output, uint32_t value) {
    const char bytes[] = {static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff),
                          static_cast<char>((value >> 16) & 0xff), static_cast<char>((value >> 24) & 0xff)};
    output.write(bytes, 4);
}

bool saveWav(const std::wstring& path, const std::vector<float>& stereo, uint32_t sampleRate) {
    std::ofstream output(std::filesystem::path(path), std::ios::binary);
    if (!output)
        return false;
    const uint32_t dataSize = static_cast<uint32_t>(stereo.size() * 2);
    output.write("RIFF", 4);
    write32(output, 36 + dataSize);
    output.write("WAVEfmt ", 8);
    write32(output, 16);
    write16(output, 1);
    write16(output, 2);
    write32(output, sampleRate);
    write32(output, sampleRate * 4);
    write16(output, 4);
    write16(output, 16);
    output.write("data", 4);
    write32(output, dataSize);
    for (float value : stereo)
        write16(output, static_cast<uint16_t>(static_cast<int16_t>(
            std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f))));
    return static_cast<bool>(output);
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace samplermode;
    if (argc < 3) {
        std::wcerr << L"Использование: sampler-diagnostic.exe <папка-библиотеки> <конфигурация.txt> [результат.wav]\n";
        return 2;
    }
    std::wstring error;
    auto library = loadLibrary(argv[1], argv[2], error);
    if (!library) {
        std::wcerr << L"ОШИБКА: " << error << L"\n";
        return 1;
    }
    std::wcout << L"Загружено сопоставлений: " << library->mappings.size() << L"\n";
    for (const auto& warning : library->warnings)
        std::wcout << L"Предупреждение: " << warning << L"\n";

    constexpr uint32_t sampleRate = 48000;
    constexpr int frames = sampleRate * 2;
    SamplerEngine engine;
    engine.prepare(sampleRate);
    engine.setLibrary(library);
    Parameters parameters;
    parameters.attackSeconds = 0.001f;
    parameters.decaySeconds = 0.01f;
    parameters.releaseSeconds = 0.01f;
    engine.setParameters(parameters);

    const auto& mapping = library->mappings.front();
    std::vector<MidiEvent> events(2);
    events[0].type = mapping.onRelease ? MidiEvent::Type::NoteOff : MidiEvent::Type::NoteOn;
    events[0].note = mapping.note;
    events[0].velocity = std::max(1, mapping.velocityMin);
    events[1].type = MidiEvent::Type::NoteOff;
    events[1].sampleOffset = sampleRate;
    events[1].note = mapping.note;
    events[1].velocity = events[0].velocity;

    std::vector<std::vector<float>> channels(kOutputChannels, std::vector<float>(frames, 0.0f));
    float* pointers[kOutputChannels];
    for (int channel = 0; channel < kOutputChannels; ++channel)
        pointers[channel] = channels[channel].data();
    engine.process(pointers, kOutputChannels, frames, events);

    float peak = 0.0f;
    for (int channel = 0; channel < kOutputChannels; ++channel) {
        for (float value : channels[channel])
            peak = std::max(peak, std::abs(value));
    }
    if (peak <= 0.000001f) {
        std::wcerr << L"ОШИБКА: семпл загружен, но звуковой сигнал не сформирован.\n";
        return 1;
    }
    const std::wstring outputPath = argc >= 4 ? argv[3] : L"sampler-diagnostic.wav";
    std::vector<float> stereo(frames * 2, 0.0f);
    for (int frame = 0; frame < frames; ++frame) {
        stereo[frame * 2] = channels[mapping.outputOffset][frame];
        stereo[frame * 2 + 1] = channels[mapping.outputOffset + 1][frame];
    }
    if (!saveWav(outputPath, stereo, sampleRate)) {
        std::wcerr << L"ОШИБКА: не удалось записать " << outputPath << L"\n";
        return 1;
    }
    std::wcout << L"ОК: сформирован звуковой сигнал, пик " << peak << L". Файл: " << outputPath << L"\n";
    return 0;
}
