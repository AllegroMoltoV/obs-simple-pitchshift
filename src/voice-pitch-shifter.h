#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <signalsmith-stretch/signalsmith-stretch.h>

class VoicePitchShifter {
public:
	VoicePitchShifter()
	{
		prepare(at44100, 44100);
		prepare(at48000, 48000);
	}

	void reset() { resetPending = true; }

	uint32_t process(float *left, float *right, uint32_t frames, uint32_t rate, int semitones)
	{
		if ((rate != 44100 && rate != 48000) || semitones == 0) {
			resetPending = true;
			return 0;
		}
		auto &stretch = rate == 44100 ? at44100 : at48000;
		semitones = std::clamp(semitones, -12, 12);
		if (resetPending || rate != previousRate || semitones != previousSemitones) {
			stretch.reset();
			stretch.setTransposeSemitones(float(semitones));
			previousRate = rate;
			previousSemitones = semitones;
			resetPending = false;
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

private:
	static void prepare(signalsmith::stretch::SignalsmithStretch<float> &stretch, uint32_t rate)
	{
		stretch.configure(2, int(rate * 40 / 1000), int(rate * 10 / 1000), false);
		stretch.setFormantFactor(1, true);
		stretch.setFormantBase(180.0f / float(rate));
	}

	// Prepare both supported rates before audio callbacks; rate changes do not allocate.
	signalsmith::stretch::SignalsmithStretch<float> at44100;
	signalsmith::stretch::SignalsmithStretch<float> at48000;
	std::array<std::array<float, 1024>, 2> scratch{};
	uint32_t previousRate = 0;
	int previousSemitones = 0;
	bool resetPending = true;
};
