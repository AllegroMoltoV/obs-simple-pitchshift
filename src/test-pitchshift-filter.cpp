#include <obs-module.h>
#include <plugin-support.h>
#include <algorithm>
#include <atomic>
#include <cmath>

#include "voice-pitch-shifter.h"

namespace {
constexpr const char *kFilterId = "simple_pitchshift_filter";
constexpr const char *kSettingSemitone = "semitone";
constexpr const char *kSettingWindow = "window_ms";
constexpr const char *kSettingFormant = "formant";
constexpr const char *kSettingGain = "gain";

struct PitchShiftFilter {
	obs_source_t *context;
	std::atomic<float> semitone{0.0f};
	std::atomic<int> windowMs{VoicePitchShifter::kDefaultWindowMs};
	std::atomic<float> formant{0.0f};
	std::atomic<float> gain{1.0f};
	std::atomic<bool> resetRequested{false};
	// Audio-thread state.
	float appliedGain = 1.0f;
	VoicePitchShifter shifter;
};

void enabled_changed(void *data, calldata_t *)
{
	static_cast<PitchShiftFilter *>(data)->resetRequested.store(true, std::memory_order_release);
}

float clamped(obs_data_t *settings, const char *key, double limit)
{
	return float(std::clamp(obs_data_get_double(settings, key), -limit, limit));
}

void update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<PitchShiftFilter *>(data);
	filter->semitone.store(clamped(settings, kSettingSemitone, 12.0), std::memory_order_relaxed);
	filter->windowMs.store(VoicePitchShifter::normalizeWindowMs(int(obs_data_get_int(settings, kSettingWindow))),
			       std::memory_order_relaxed);
	filter->formant.store(clamped(settings, kSettingFormant, 12.0), std::memory_order_relaxed);
	filter->gain.store(std::pow(10.0f, clamped(settings, kSettingGain, 30.0) / 20.0f), std::memory_order_relaxed);
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new PitchShiftFilter();
	filter->context = source;
	update(filter, settings);
	filter->appliedGain = filter->gain.load(std::memory_order_relaxed);
	signal_handler_connect(obs_source_get_signal_handler(source), "enable", enabled_changed, filter);
	return filter;
}

void destroy(void *data)
{
	auto *filter = static_cast<PitchShiftFilter *>(data);
	signal_handler_disconnect(obs_source_get_signal_handler(filter->context), "enable", enabled_changed, filter);
	delete filter;
}

const char *name(void *)
{
	return obs_module_text("SimplePitchShiftFilterName");
}

void defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, kSettingSemitone, 0.0);
	obs_data_set_default_int(settings, kSettingWindow, VoicePitchShifter::kDefaultWindowMs);
	obs_data_set_default_double(settings, kSettingFormant, 0.0);
	obs_data_set_default_double(settings, kSettingGain, 0.0);
}

obs_properties_t *properties(void *)
{
	auto *props = obs_properties_create();
	obs_properties_add_float_slider(props, kSettingSemitone, obs_module_text("SimplePitchShiftSemitone"), -12.0,
					12.0, 0.1);
	obs_properties_add_float_slider(props, kSettingFormant, obs_module_text("SimplePitchShiftFormant"), -12.0, 12.0,
					0.1);
	auto *gain = obs_properties_add_float_slider(props, kSettingGain, obs_module_text("SimplePitchShiftGain"),
						     -30.0, 30.0, 0.1);
	obs_property_float_set_suffix(gain, " dB");
	auto *window = obs_properties_add_list(props, kSettingWindow, obs_module_text("SimplePitchShiftWindow"),
					       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(window, obs_module_text("SimplePitchShiftWindow10"), 10);
	obs_property_list_add_int(window, obs_module_text("SimplePitchShiftWindow20"), 20);
	obs_property_list_add_int(window, obs_module_text("SimplePitchShiftWindow40"), 40);
	return props;
}

// Ramps from the previous block's gain to the current setting so slider moves do not click.
void apply_gain(PitchShiftFilter *filter, obs_audio_data *audio, size_t channels)
{
	const float target = filter->gain.load(std::memory_order_relaxed);
	const float start = filter->appliedGain;
	filter->appliedGain = target;
	if (start == 1.0f && target == 1.0f)
		return;
	for (size_t c = 0; c < channels && c < MAX_AV_PLANES; ++c) {
		auto *samples = reinterpret_cast<float *>(audio->data[c]);
		if (!samples)
			continue;
		for (uint32_t i = 0; i < audio->frames; ++i)
			samples[i] *= start + (target - start) * float(i + 1) / float(audio->frames);
	}
}

obs_audio_data *process(void *data, obs_audio_data *audio)
{
	auto *filter = static_cast<PitchShiftFilter *>(data);
	auto *output = obs_get_audio();
	const auto rate = audio_output_get_sample_rate(output);
	const auto channels = audio_output_get_channels(output);
	if (channels != 2 || (rate != 44100 && rate != 48000)) {
		filter->shifter.reset();
		apply_gain(filter, audio, channels);
		return audio;
	}
	if (filter->resetRequested.exchange(false, std::memory_order_acq_rel))
		filter->shifter.reset();
	const auto latency = filter->shifter.process(reinterpret_cast<float *>(audio->data[0]),
						     reinterpret_cast<float *>(audio->data[1]), audio->frames, rate,
						     filter->windowMs.load(std::memory_order_relaxed),
						     filter->semitone.load(std::memory_order_relaxed),
						     filter->formant.load(std::memory_order_relaxed));
	apply_gain(filter, audio, channels);
	// Match OBS's noise-suppression filter: timestamp describes the delayed audio content.
	const uint64_t delay = uint64_t(latency) * 1000000000ULL / rate;
	audio->timestamp = audio->timestamp >= delay ? audio->timestamp - delay : 0;
	return audio;
}
} // namespace

void register_simple_pitchshift_filter(void)
{
	obs_source_info info = {};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = name;
	info.create = create;
	info.update = update;
	info.destroy = destroy;
	info.get_defaults = defaults;
	info.get_properties = properties;
	info.filter_audio = process;
	obs_register_source(&info);
}
