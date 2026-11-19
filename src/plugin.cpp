#include "plugin.h"

#include "editor_win32.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace samplermode {
namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;

constexpr uint32 kProcessorUid[] = {0x4A3B2C10, 0x7E8F4A61, 0xB21F93D4, 0x56C0A811};
constexpr uint32 kControllerUid[] = {0x5B4C3D21, 0x8F904B72, 0xC320A4E5, 0x67D1B922};

std::string toUtf8(const std::wstring& value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

std::wstring fromUtf8(const char* value) {
    if (!value || !*value)
        return {};
    const int length = static_cast<int>(std::strlen(value));
    const int size = MultiByteToWideChar(CP_UTF8, 0, value, length, nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, length, result.data(), size);
    return result;
}

double defaultNormalized(ParamID id) {
    switch (id) {
    case kVolume: return 60.0 / 72.0;
    case kPan: return 0.5;
    case kAttack: return (0.01 - 0.001) / 4.999;
    case kDecay: return (0.1 - 0.001) / 4.999;
    case kSustain: return 1.0;
    case kRelease: return (0.1 - 0.001) / 9.999;
    case kPitchBendValue: return 0.5;
    default: return 0.0;
    }
}

RangeParameter* rangeParameter(const TChar* title, ParamID id, const TChar* units,
                               double minimum, double maximum, double initial, int precision = 2) {
    auto* parameter = new RangeParameter(title, id, units, minimum, maximum, initial);
    parameter->setPrecision(precision);
    return parameter;
}

StringListParameter* listParameter(const TChar* title, ParamID id,
                                   std::initializer_list<const TChar*> values) {
    auto* parameter = new StringListParameter(title, id);
    for (const auto* value : values)
        parameter->appendString(value);
    return parameter;
}

bool readState(IBStream* state, std::array<double, 9>& values,
               std::wstring& rootPath, std::wstring& configuration) {
    IBStreamer streamer(state, kLittleEndian);
    int32 version = 0;
    if (!streamer.readInt32(version) || version != 1)
        return false;
    for (double& value : values) {
        if (!streamer.readDouble(value) || value < 0.0 || value > 1.0)
            return false;
    }
    char* root = streamer.readStr8();
    char* config = streamer.readStr8();
    if (!root || !config) {
        delete[] root;
        delete[] config;
        return false;
    }
    rootPath = fromUtf8(root);
    configuration = fromUtf8(config);
    delete[] root;
    delete[] config;
    return true;
}

bool writeState(IBStream* state, const std::array<double, 9>& values,
                const std::wstring& rootPath, const std::wstring& configuration) {
    IBStreamer streamer(state, kLittleEndian);
    if (!streamer.writeInt32(1))
        return false;
    for (double value : values) {
        if (!streamer.writeDouble(value))
            return false;
    }
    const std::string root = toUtf8(rootPath);
    const std::string config = toUtf8(configuration);
    return streamer.writeStr8(root.c_str()) && streamer.writeStr8(config.c_str());
}
}

SamplerProcessor::SamplerProcessor() {
    setControllerClass(FUID(kControllerUid[0], kControllerUid[1], kControllerUid[2], kControllerUid[3]));
    for (ParamID id = kVolume; id <= kModMode; ++id)
        normalizedValues_[id] = defaultNormalized(id);
}

tresult PLUGIN_API SamplerProcessor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;
    addEventInput(STR16("MIDI Input"), 1);
    addAudioOutput(STR16("Output 1/2"), SpeakerArr::kStereo, kMain);
    for (int pair = 1; pair < kOutputChannels / 2; ++pair) {
        const std::wstring name = L"Output " + std::to_wstring(pair * 2 + 1) + L"/" +
            std::to_wstring(pair * 2 + 2);
        auto* bus = new AudioBus(reinterpret_cast<const TChar*>(name.c_str()), kAux,
                                 BusInfo::kDefaultActive, SpeakerArr::kStereo);
        audioOutputs.push_back(IPtr<Bus>(bus, false));
    }
    return kResultOk;
}

tresult PLUGIN_API SamplerProcessor::setupProcessing(ProcessSetup& setup) {
    engine_.prepare(setup.sampleRate);
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API SamplerProcessor::setActive(TBool state) {
    if (!state)
        engine_.allNotesOff();
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API SamplerProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API SamplerProcessor::process(ProcessData& data) {
    std::vector<MidiEvent> events;
    if (data.inputParameterChanges) {
        const int count = data.inputParameterChanges->getParameterCount();
        for (int index = 0; index < count; ++index) {
            auto* queue = data.inputParameterChanges->getParameterData(index);
            if (!queue || queue->getPointCount() == 0)
                continue;
            int sampleOffset = 0;
            ParamValue value = 0.0;
            queue->getPoint(queue->getPointCount() - 1, sampleOffset, value);
            const ParamID id = queue->getParameterId();
            if (id == kModWheelValue || id == kPitchBendValue) {
                MidiEvent event;
                event.type = id == kModWheelValue ? MidiEvent::Type::ModWheel : MidiEvent::Type::PitchBend;
                event.sampleOffset = sampleOffset;
                event.value = id == kModWheelValue
                    ? static_cast<int>(std::lround(value * 127.0))
                    : static_cast<int>(std::lround(value * 16383.0));
                events.push_back(event);
            } else if (id <= kModMode) {
                normalizedValues_[id] = value;
            }
        }
    }

    if (data.inputEvents) {
        const int count = data.inputEvents->getEventCount();
        for (int index = 0; index < count; ++index) {
            Event source {};
            if (data.inputEvents->getEvent(index, source) != kResultOk)
                continue;
            MidiEvent event;
            event.sampleOffset = source.sampleOffset;
            switch (source.type) {
            case Event::kNoteOnEvent:
                event.type = MidiEvent::Type::NoteOn;
                event.note = source.noteOn.pitch;
                event.velocity = static_cast<int>(std::lround(source.noteOn.velocity * 127.0f));
                events.push_back(event);
                break;
            case Event::kNoteOffEvent:
                event.type = MidiEvent::Type::NoteOff;
                event.note = source.noteOff.pitch;
                event.velocity = static_cast<int>(std::lround(source.noteOff.velocity * 127.0f));
                events.push_back(event);
                break;
            case Event::kLegacyMIDICCOutEvent:
                if (source.midiCCOut.controlNumber == kCtrlModWheel) {
                    event.type = MidiEvent::Type::ModWheel;
                    event.value = source.midiCCOut.value;
                    events.push_back(event);
                } else if (source.midiCCOut.controlNumber == kPitchBend) {
                    event.type = MidiEvent::Type::PitchBend;
                    event.value = static_cast<uint8_t>(source.midiCCOut.value) |
                        (static_cast<int>(static_cast<uint8_t>(source.midiCCOut.value2)) << 7);
                    events.push_back(event);
                }
                break;
            default:
                break;
            }
        }
    }
    std::sort(events.begin(), events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.sampleOffset < b.sampleOffset;
    });

    currentParameters_ = parametersFromNormalized();
    engine_.setParameters(currentParameters_);
    float* outputs[kOutputChannels] = {};
    int channelCount = 0;
    for (int bus = 0; bus < data.numOutputs && channelCount < kOutputChannels; ++bus) {
        auto& buffer = data.outputs[bus];
        for (int channel = 0; channel < buffer.numChannels && channelCount < kOutputChannels; ++channel)
            outputs[channelCount++] = buffer.channelBuffers32[channel];
    }
    engine_.process(outputs, channelCount, data.numSamples, events);
    for (int bus = 0; bus < data.numOutputs; ++bus) {
        data.outputs[bus].silenceFlags = engine_.activeVoiceCount() == 0
            ? getChannelMask(data.outputs[bus].numChannels) : 0;
    }
    return kResultOk;
}

Parameters SamplerProcessor::parametersFromNormalized() const {
    Parameters result;
    result.volumeDb = static_cast<float>(normalizedValues_[kVolume] * 72.0 - 60.0);
    result.pan = static_cast<float>(normalizedValues_[kPan] * 2.0 - 1.0);
    result.attackSeconds = static_cast<float>(normalizedValues_[kAttack] * 4.999 + 0.001);
    result.decaySeconds = static_cast<float>(normalizedValues_[kDecay] * 4.999 + 0.001);
    result.sustain = static_cast<float>(normalizedValues_[kSustain]);
    result.releaseSeconds = static_cast<float>(normalizedValues_[kRelease] * 9.999 + 0.001);
    result.sustainPedal = normalizedValues_[kSustainPedal] >= 0.5;
    result.pitchBendRange = static_cast<int>(std::lround(normalizedValues_[kPitchBendRange] * 11.0)) + 1;
    result.modMode = static_cast<int>(std::lround(normalizedValues_[kModMode] * 2.0));
    return result;
}

tresult PLUGIN_API SamplerProcessor::setState(IBStream* state) {
    std::wstring root;
    std::wstring config;
    if (!readState(state, normalizedValues_, root, config))
        return kResultFalse;
    loadLibrary(root, config);
    return kResultOk;
}

tresult PLUGIN_API SamplerProcessor::getState(IBStream* state) {
    std::lock_guard<std::mutex> lock(libraryMutex_);
    return writeState(state, normalizedValues_, libraryRoot_, configName_) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API SamplerProcessor::notify(IMessage* message) {
    if (!message || !FIDStringsEqual(message->getMessageID(), "SamplerLibrary"))
        return AudioEffect::notify(message);
    auto* attributes = message->getAttributes();
    if (!attributes)
        return kResultFalse;
    TChar root[4096] = {};
    TChar config[1024] = {};
    if (attributes->getString("Root", root, sizeof(root)) != kResultTrue ||
        attributes->getString("Config", config, sizeof(config)) != kResultTrue)
        return kResultFalse;
    loadLibrary(reinterpret_cast<const wchar_t*>(root), reinterpret_cast<const wchar_t*>(config));
    return kResultTrue;
}

void SamplerProcessor::loadLibrary(const std::wstring& root, const std::wstring& config) {
    {
        std::lock_guard<std::mutex> lock(libraryMutex_);
        libraryRoot_ = root;
        configName_ = config;
    }
    if (root.empty() || config.empty()) {
        engine_.setLibrary({});
        return;
    }
    std::wstring error;
    engine_.setLibrary(samplermode::loadLibrary(root, config, error));
}

tresult PLUGIN_API SamplerPlugin::initialize(FUnknown* context) {
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    parameters.addParameter(rangeParameter(STR16("Volume"), kVolume, STR16("dB"), -60.0, 12.0, 0.0, 1));
    parameters.addParameter(rangeParameter(STR16("Pan"), kPan, STR16(""), -1.0, 1.0, 0.0));
    parameters.addParameter(rangeParameter(STR16("Attack"), kAttack, STR16("s"), 0.001, 5.0, 0.01, 3));
    parameters.addParameter(rangeParameter(STR16("Decay"), kDecay, STR16("s"), 0.001, 5.0, 0.1, 3));
    parameters.addParameter(rangeParameter(STR16("Sustain"), kSustain, STR16(""), 0.0, 1.0, 1.0));
    parameters.addParameter(rangeParameter(STR16("Release"), kRelease, STR16("s"), 0.001, 10.0, 0.1, 3));
    parameters.addParameter(listParameter(STR16("Sustain Pedal"), kSustainPedal,
                                          {STR16("Pedal Off"), STR16("Pedal On")}));
    auto* pitchRange = listParameter(STR16("Pitch Bend Range"), kPitchBendRange, {});
    for (int semitones = 1; semitones <= 12; ++semitones) {
        const std::wstring text = std::to_wstring(semitones) + L" semitone" + (semitones == 1 ? L"" : L"s");
        pitchRange->appendString(reinterpret_cast<const TChar*>(text.c_str()));
    }
    parameters.addParameter(pitchRange);
    parameters.addParameter(listParameter(STR16("Mod Wheel"), kModMode,
                                          {STR16("Mod Off"), STR16("Cut Off"), STR16("Vibrato")}));
    auto* modWheel = rangeParameter(STR16("MIDI Mod Wheel Value"), kModWheelValue,
                                    STR16(""), 0.0, 1.0, 0.0);
    modWheel->getInfo().flags = ParameterInfo::kIsHidden;
    parameters.addParameter(modWheel);
    auto* pitchBend = rangeParameter(STR16("MIDI Pitch Bend Value"), kPitchBendValue,
                                     STR16(""), 0.0, 1.0, 0.5);
    pitchBend->getInfo().flags = ParameterInfo::kIsHidden;
    parameters.addParameter(pitchBend);
    for (int index = 0; index < parameters.getParameterCount(); ++index) {
        auto* parameter = parameters.getParameterByIndex(index);
        parameter->setNormalized(parameter->getInfo().defaultNormalizedValue);
    }
    return kResultOk;
}

void SamplerPlugin::applyDefaultsOnce() {
    if (defaultsApplied_ || stateLoaded_)
        return;
    defaultsApplied_ = true;
    for (int index = 0; index < parameters.getParameterCount(); ++index) {
        auto* parameter = parameters.getParameterByIndex(index);
        parameter->setNormalized(parameter->getInfo().defaultNormalizedValue);
    }
}

tresult PLUGIN_API SamplerPlugin::setState(IBStream* state) {
    std::array<double, 9> values {};
    std::wstring root;
    std::wstring config;
    if (!readState(state, values, root, config))
        return kResultFalse;
    stateLoaded_ = true;
    defaultsApplied_ = true;
    for (ParamID id = kVolume; id <= kModMode; ++id)
        setParamNormalized(id, values[id]);
    if (!root.empty() && !config.empty())
        selectLibrary(root, config);
    refreshEditor();
    return kResultOk;
}

tresult PLUGIN_API SamplerPlugin::getState(IBStream* state) {
    std::array<double, 9> values {};
    for (ParamID id = kVolume; id <= kModMode; ++id)
        values[id] = normalized(id);
    return writeState(state, values, libraryRoot(), configName()) ? kResultOk : kResultFalse;
}

IPlugView* PLUGIN_API SamplerPlugin::createView(const char* name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor)) {
        applyDefaultsOnce();
        return new SamplerEditor(this);
    }
    return nullptr;
}

tresult PLUGIN_API SamplerPlugin::getMidiControllerAssignment(
    int32 busIndex, int16, CtrlNumber controller, ParamID& id) {
    if (busIndex != 0)
        return kResultFalse;
    if (controller == kCtrlModWheel) {
        id = kModWheelValue;
        return kResultTrue;
    }
    if (controller == kPitchBend) {
        id = kPitchBendValue;
        return kResultTrue;
    }
    return kResultFalse;
}

void SamplerPlugin::setParameterFromUi(ParamID id, ParamValue value) {
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}

double SamplerPlugin::normalized(ParamID id) const {
    const auto* parameter = const_cast<SamplerPlugin*>(this)->getParameterObject(id);
    return parameter ? parameter->getNormalized() : 0.0;
}

void SamplerPlugin::selectLibrary(const std::wstring& root, const std::wstring& config) {
    std::wstring error;
    auto library = samplermode::loadLibrary(root, config, error);
    {
        std::lock_guard<std::mutex> lock(libraryMutex_);
        libraryRoot_ = root;
        configName_ = config;
        if (library) {
            libraryStatus_ = L"Загружено семплов: " + std::to_wstring(library->mappings.size()) + L".";
            if (!library->warnings.empty())
                libraryStatus_ += L" Предупреждений: " + std::to_wstring(library->warnings.size()) +
                    L". " + library->warnings.front();
        } else {
            libraryStatus_ = error;
        }
    }

    if (auto* message = allocateMessage()) {
        message->setMessageID("SamplerLibrary");
        auto* attributes = message->getAttributes();
        attributes->setString("Root", reinterpret_cast<const TChar*>(root.c_str()));
        attributes->setString("Config", reinterpret_cast<const TChar*>(config.c_str()));
        sendMessage(message);
        message->release();
    }
    setDirty(true);
    refreshEditor();
}

std::wstring SamplerPlugin::libraryRoot() const {
    std::lock_guard<std::mutex> lock(libraryMutex_);
    return libraryRoot_;
}

std::wstring SamplerPlugin::configName() const {
    std::lock_guard<std::mutex> lock(libraryMutex_);
    return configName_;
}

std::wstring SamplerPlugin::libraryStatus() const {
    std::lock_guard<std::mutex> lock(libraryMutex_);
    return libraryStatus_;
}

void SamplerPlugin::editorOpened(SamplerEditor* editor) { editor_ = editor; }
void SamplerPlugin::editorClosed(SamplerEditor* editor) {
    if (editor_ == editor)
        editor_ = nullptr;
}
void SamplerPlugin::refreshEditor() {
    if (editor_)
        editor_->refresh();
}

} // namespace samplermode

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF("SamplerMode", "https://github.com/ggptshka-sys/samplermode", "")
DEF_CLASS2(INLINE_UID(0x4A3B2C10, 0x7E8F4A61, 0xB21F93D4, 0x56C0A811),
           PClassInfo::kManyInstances, kVstAudioEffectClass, "SamplerMode",
           Vst::kDistributable, "Instrument|Sampler", "1.0.0", kVstVersionString,
           samplermode::SamplerProcessor::createInstance)
DEF_CLASS2(INLINE_UID(0x5B4C3D21, 0x8F904B72, 0xC320A4E5, 0x67D1B922),
           PClassInfo::kManyInstances, kVstComponentControllerClass, "SamplerMode Controller",
           0, "", "1.0.0", kVstVersionString,
           samplermode::SamplerPlugin::createInstance)
END_FACTORY
