#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <signalsmith-stretch/signalsmith-stretch.h>

class VoicePitchShifter {
public:
	static constexpr std::array<int, 3> kWindowMs{20, 30, 40};
	static constexpr int kDefaultWindowMs = 40;

	VoicePitchShifter()
	{
		for (size_t w = 0; w < kWindowMs.size(); ++w) {
			prepare(at44100[w], 44100, kWindowMs[w]);
			prepare(at48000[w], 48000, kWindowMs[w]);
		}
	}

	void reset() { resetPending = true; }

	// Returns the DSP latency in samples, or 0 when the audio passes through unchanged.
	uint32_t process(float *left, float *right, uint32_t frames, uint32_t rate, int windowMs, float semitones,
			 float formantSemitones)
	{
		semitones = std::clamp(semitones, -12.0f, 12.0f);
		formantSemitones = std::clamp(formantSemitones, -12.0f, 12.0f);
		if ((rate != 44100 && rate != 48000) || (semitones == 0.0f && formantSemitones == 0.0f)) {
			resetPending = true;
			return 0;
		}
		const size_t window = windowIndex(windowMs);
		auto &stretch = rate == 44100 ? at44100[window] : at48000[window];
		// Only a change of processor or bypass history clears audio; pitch and formant follow sliders without gaps.
		if (resetPending || rate != previousRate || window != previousWindow) {
			stretch.reset();
			previousRate = rate;
			previousWindow = window;
			resetPending = false;
			previousSemitones = semitones + 1.0f;
			previousFormant = formantSemitones + 1.0f;
		}
		if (semitones != previousSemitones) {
			stretch.setTransposeSemitones(semitones);
			previousSemitones = semitones;
		}
		if (formantSemitones != previousFormant) {
			stretch.setFormantSemitones(formantSemitones, true);
			previousFormant = formantSemitones;
		}
		// Scratch capacity is a processing chunk, not a limit on OBS input frames.
		for (uint32_t offset = 0; offset < frames;) {
			const auto count = std::min<uint32_t>(frames - offset, uint32_t(scratch[0].size()));
			const float *input[] = {left + offset, right + offset};
			float *output[] = {scratch[0].data(), scratch[1].data()};
			stretch.process(input, int(count), output, int(count));
			std::copy_n(output[0], count, left + offset);
			std::copy_n(output[1], count, right + offset);
			offset += count;
		}
		return uint32_t(stretch.inputLatency() + stretch.outputLatency());
	}

	static int normalizeWindowMs(int windowMs) { return kWindowMs[windowIndex(windowMs)]; }

private:
	using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

	static size_t windowIndex(int windowMs)
	{
		for (size_t w = 0; w < kWindowMs.size(); ++w)
			if (kWindowMs[w] == windowMs)
				return w;
		return windowIndex(kDefaultWindowMs);
	}

	static void prepare(Stretch &stretch, uint32_t rate, int windowMs)
	{
		// Interval stays at a quarter of the window, the ratio adopted for v1.0.0.
		stretch.configure(2, int(rate * windowMs / 1000), int(rate * windowMs / 4000), false);
		stretch.setFormantFactor(1, true);
		stretch.setFormantBase(180.0f / float(rate));
	}

	// Prepare every supported rate and window before audio callbacks; switching does not allocate.
	std::array<Stretch, kWindowMs.size()> at44100;
	std::array<Stretch, kWindowMs.size()> at48000;
	std::array<std::array<float, 1024>, 2> scratch{};
	uint32_t previousRate = 0;
	size_t previousWindow = 0;
	float previousSemitones = 0.0f;
	float previousFormant = 0.0f;
	bool resetPending = true;
};
