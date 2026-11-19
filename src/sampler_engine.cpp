#include "sampler_engine.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace samplermode {
namespace {
constexpr double kPi = 3.14159265358979323846;

float decibelsToGain(float decibels) {
    return decibels <= -140.0f ? 0.0f : std::pow(10.0f, decibels / 20.0f);
}
}

SamplerEngine::SamplerEngine() {
    noteOffVelocity_.fill(0);
}

void SamplerEngine::prepare(double sampleRate) {
    sampleRate_ = std::max(8000.0, sampleRate);
    allNotesOff();
}

void SamplerEngine::setLibrary(std::shared_ptr<const SampleLibrary> library) {
    std::atomic_store(&library_, std::move(library));
}

std::shared_ptr<const SampleLibrary> SamplerEngine::library() const {
    return std::atomic_load(&library_);
}

void SamplerEngine::setParameters(const Parameters& parameters) {
    parameters_ = parameters;
}

void SamplerEngine::allNotesOff() {
    for (auto& voice : voices_) {
        voice.active = false;
        voice.sample.reset();
    }
}

int SamplerEngine::activeVoiceCount() const {
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(),
                                         [](const Voice& voice) { return voice.active; }));
}

void SamplerEngine::process(float* const* outputs, int channelCount, int frameCount,
                            const std::vector<MidiEvent>& events) {
    if (frameCount <= 0)
        return;
    for (int channel = 0; channel < channelCount; ++channel) {
        if (outputs[channel])
            std::fill_n(outputs[channel], frameCount, 0.0f);
    }

    if (previousParameters_.sustainPedal && !parameters_.sustainPedal)
        releaseSustained();
    previousParameters_ = parameters_;

    size_t eventIndex = 0;
    const float masterGain = decibelsToGain(parameters_.volumeDb);
    const float leftPan = std::cos((parameters_.pan + 1.0f) * 0.25f * static_cast<float>(kPi));
    const float rightPan = std::sin((parameters_.pan + 1.0f) * 0.25f * static_cast<float>(kPi));
    const float cutoff = 200.0f * std::pow(100.0f, static_cast<float>(modWheel_));
    const float filterAlpha = 1.0f - std::exp(-2.0f * static_cast<float>(kPi) * cutoff /
                                               static_cast<float>(sampleRate_));

    for (int frame = 0; frame < frameCount; ++frame) {
        while (eventIndex < events.size() && events[eventIndex].sampleOffset <= frame) {
            handleEvent(events[eventIndex]);
            ++eventIndex;
        }

        const double vibrato = parameters_.modMode == 2
            ? 0.25 * modWheel_ * std::sin(2.0 * kPi * lfoPhase_)
            : 0.0;
        const double pitchRatio = std::pow(2.0,
            (pitchBend_ * parameters_.pitchBendRange + vibrato) / 12.0);
        lfoPhase_ += 5.5 / sampleRate_;
        if (lfoPhase_ >= 1.0)
            lfoPhase_ -= 1.0;

        for (auto& voice : voices_) {
            if (!voice.active || !voice.sample)
                continue;
            auto& sample = *voice.sample;
            const size_t frameCountInSample = sample.channels
                ? sample.frames.size() / sample.channels : 0;
            if (voice.position >= static_cast<double>(frameCountInSample)) {
                voice.active = false;
                voice.sample.reset();
                continue;
            }

            advanceEnvelope(voice);
            if (!voice.active)
                continue;

            const size_t first = static_cast<size_t>(voice.position);
            const size_t second = std::min(first + 1, frameCountInSample - 1);
            const float fraction = static_cast<float>(voice.position - first);
            const auto readChannel = [&](size_t frameIndex, uint32_t channel) {
                const uint32_t sourceChannel = std::min(channel, sample.channels - 1);
                return sample.frames[frameIndex * sample.channels + sourceChannel];
            };
            float left = readChannel(first, 0) + fraction *
                (readChannel(second, 0) - readChannel(first, 0));
            float right = sample.channels == 1 ? left
                : readChannel(first, 1) + fraction *
                    (readChannel(second, 1) - readChannel(first, 1));

            if (parameters_.modMode == 1) {
                voice.filter[0] += filterAlpha * (left - voice.filter[0]);
                voice.filter[1] += filterAlpha * (right - voice.filter[1]);
                left = voice.filter[0];
                right = voice.filter[1];
            }

            const float gain = masterGain * voice.velocityGain * voice.sampleGain * voice.envelope;
            left *= gain * leftPan;
            right *= gain * rightPan;
            const int output = voice.outputOffset;
            if (output + 1 < channelCount && outputs[output] && outputs[output + 1]) {
                outputs[output][frame] += left;
                outputs[output + 1][frame] += right;
            }

            voice.position += voice.sourceRateRatio * pitchRatio;
        }
    }
}

void SamplerEngine::handleEvent(const MidiEvent& event) {
    switch (event.type) {
    case MidiEvent::Type::NoteOn:
        if (event.velocity > 0)
            noteOn(event.note, event.velocity);
        else
            noteOff(event.note, 64);
        break;
    case MidiEvent::Type::NoteOff:
        noteOff(event.note, event.velocity);
        break;
    case MidiEvent::Type::PitchBend:
        pitchBend_ = (std::clamp(event.value, 0, 16383) - 8192) / 8192.0;
        break;
    case MidiEvent::Type::ModWheel:
        modWheel_ = std::clamp(event.value, 0, 127) / 127.0;
        break;
    }
}

void SamplerEngine::noteOn(int note, int velocity) {
    if (note < 0 || note > 127)
        return;
    noteOffVelocity_[note] = velocity;
    auto currentLibrary = std::atomic_load(&library_);
    if (!currentLibrary)
        return;
    for (const auto& mapping : currentLibrary->mappings) {
        if (mapping.onRelease || mapping.note != note || velocity < mapping.velocityMin ||
            velocity > mapping.velocityMax)
            continue;
        if (mapping.muteNote != 0) {
            for (auto& voice : voices_) {
                if (voice.active && voice.note == mapping.muteNote)
                    releaseVoice(voice, true);
            }
        }
        startSample(mapping, note, velocity, false);
    }
}

void SamplerEngine::noteOff(int note, int velocity) {
    if (note < 0 || note > 127)
        return;
    if (parameters_.sustainPedal) {
        for (auto& voice : voices_) {
            if (voice.active && voice.note == note && !voice.oneShot)
                voice.sustained = true;
        }
        return;
    }

    const int effectiveVelocity = noteOffVelocity_[note] > 0 ? noteOffVelocity_[note] : velocity;
    for (auto& voice : voices_) {
        if (voice.active && voice.note == note && !voice.oneShot)
            releaseVoice(voice);
    }
    auto currentLibrary = std::atomic_load(&library_);
    if (!currentLibrary)
        return;
    for (const auto& mapping : currentLibrary->mappings) {
        if (mapping.onRelease && mapping.note == note &&
            effectiveVelocity >= mapping.velocityMin && effectiveVelocity <= mapping.velocityMax)
            startSample(mapping, note, effectiveVelocity, true);
    }
    noteOffVelocity_[note] = 0;
}

void SamplerEngine::startSample(const SampleMapping& mapping, int note, int velocity, bool oneShot) {
    if (!mapping.sample || mapping.sample->frames.empty())
        return;
    Voice* voice = allocateVoice();
    voice->active = true;
    voice->oneShot = oneShot;
    voice->sustained = false;
    voice->age = nextVoiceAge_++;
    voice->note = note;
    voice->outputOffset = mapping.outputOffset;
    voice->position = 0.0;
    voice->sourceRateRatio = mapping.sample->sampleRate / sampleRate_;
    voice->velocityGain = static_cast<float>(velocity) / 127.0f;
    voice->sampleGain = decibelsToGain(mapping.gain);
    voice->envelope = 0.0f;
    voice->releaseStart = 0.0f;
    voice->stage = EnvelopeStage::Attack;
    voice->sample = mapping.sample;
    voice->filter.fill(0.0f);
}

SamplerEngine::Voice* SamplerEngine::allocateVoice() {
    auto free = std::find_if(voices_.begin(), voices_.end(),
                             [](const Voice& voice) { return !voice.active; });
    if (free != voices_.end())
        return &*free;
    return &*std::min_element(voices_.begin(), voices_.end(),
                              [](const Voice& a, const Voice& b) { return a.age < b.age; });
}

void SamplerEngine::releaseVoice(Voice& voice, bool fast) {
    if (!voice.active || voice.stage == EnvelopeStage::Release)
        return;
    voice.sustained = false;
    voice.releaseStart = std::max(0.0001f, voice.envelope);
    const double seconds = fast ? 0.005 : std::max(0.001f, parameters_.releaseSeconds);
    voice.stage = EnvelopeStage::Release;
    voice.releaseStart /= std::max(1.0f, static_cast<float>(seconds * sampleRate_));
}

void SamplerEngine::releaseSustained() {
    for (auto& voice : voices_) {
        if (voice.active && voice.sustained)
            releaseVoice(voice);
    }
}

void SamplerEngine::advanceEnvelope(Voice& voice) {
    switch (voice.stage) {
    case EnvelopeStage::Attack: {
        const float step = 1.0f / std::max(1.0f,
            static_cast<float>(parameters_.attackSeconds * sampleRate_));
        voice.envelope += step;
        if (voice.envelope >= 1.0f) {
            voice.envelope = 1.0f;
            voice.stage = EnvelopeStage::Decay;
        }
        break;
    }
    case EnvelopeStage::Decay: {
        const float step = (1.0f - parameters_.sustain) / std::max(1.0f,
            static_cast<float>(parameters_.decaySeconds * sampleRate_));
        voice.envelope = std::max(parameters_.sustain, voice.envelope - step);
        if (voice.envelope <= parameters_.sustain)
            voice.stage = EnvelopeStage::Sustain;
        break;
    }
    case EnvelopeStage::Sustain:
        voice.envelope = parameters_.sustain;
        break;
    case EnvelopeStage::Release:
        voice.envelope -= voice.releaseStart;
        if (voice.envelope <= 0.0f) {
            voice.envelope = 0.0f;
            voice.active = false;
            voice.sample.reset();
        }
        break;
    }
}

} // namespace samplermode
