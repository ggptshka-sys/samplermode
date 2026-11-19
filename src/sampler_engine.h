#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace samplermode {

constexpr int kOutputChannels = 16;
constexpr int kMaxVoices = 64;

struct SampleData {
    std::string name;
    uint32_t sampleRate = 44100;
    uint32_t channels = 0;
    std::vector<float> frames;
};

struct SampleMapping {
    std::shared_ptr<SampleData> sample;
    bool onRelease = false;
    int note = 1;
    int velocityMin = 0;
    int velocityMax = 127;
    float gain = 0.0f;
    int muteNote = 0;
    int outputOffset = 0;
};

struct SampleLibrary {
    std::wstring root;
    std::wstring configName;
    std::vector<SampleMapping> mappings;
    std::vector<std::wstring> warnings;
};

struct Parameters {
    float volumeDb = 0.0f;
    float pan = 0.0f;
    float attackSeconds = 0.01f;
    float decaySeconds = 0.1f;
    float sustain = 1.0f;
    float releaseSeconds = 0.1f;
    bool sustainPedal = false;
    int pitchBendRange = 1;
    int modMode = 0;
};

struct MidiEvent {
    enum class Type { NoteOn, NoteOff, PitchBend, ModWheel };
    Type type = Type::NoteOn;
    int sampleOffset = 0;
    int note = 0;
    int velocity = 64;
    int value = 8192;
};

class SamplerEngine {
public:
    SamplerEngine();

    void prepare(double sampleRate);
    void setLibrary(std::shared_ptr<const SampleLibrary> library);
    std::shared_ptr<const SampleLibrary> library() const;
    void setParameters(const Parameters& parameters);
    void allNotesOff();
    void process(float* const* outputs, int channelCount, int frameCount,
                 const std::vector<MidiEvent>& events);
    int activeVoiceCount() const;

private:
    enum class EnvelopeStage { Attack, Decay, Sustain, Release };

    struct Voice {
        bool active = false;
        bool oneShot = false;
        bool sustained = false;
        uint64_t age = 0;
        int note = 0;
        int outputOffset = 0;
        double position = 0.0;
        double sourceRateRatio = 1.0;
        float velocityGain = 1.0f;
        float sampleGain = 1.0f;
        float envelope = 0.0f;
        float releaseStart = 0.0f;
        EnvelopeStage stage = EnvelopeStage::Attack;
        std::shared_ptr<SampleData> sample;
        std::array<float, 2> filter {};
    };

    void handleEvent(const MidiEvent& event);
    void noteOn(int note, int velocity);
    void noteOff(int note, int velocity);
    void startSample(const SampleMapping& mapping, int note, int velocity, bool oneShot);
    void releaseVoice(Voice& voice, bool fast = false);
    void releaseSustained();
    void advanceEnvelope(Voice& voice);
    Voice* allocateVoice();

    double sampleRate_ = 44100.0;
    std::shared_ptr<const SampleLibrary> library_;
    Parameters parameters_ {};
    Parameters previousParameters_ {};
    std::array<Voice, kMaxVoices> voices_ {};
    std::array<int, 128> noteOffVelocity_ {};
    uint64_t nextVoiceAge_ = 1;
    double pitchBend_ = 0.0;
    double modWheel_ = 0.0;
    double lfoPhase_ = 0.0;
};

std::shared_ptr<SampleLibrary> loadLibrary(const std::wstring& root,
                                           const std::wstring& configName,
                                           std::wstring& error);
std::vector<std::wstring> listConfigurations(const std::wstring& root);
bool loadWav(const std::wstring& path, SampleData& sample, std::wstring& error);

} // namespace samplermode
