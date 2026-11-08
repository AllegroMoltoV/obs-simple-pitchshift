#include <obs-module.h>
#include <plugin-support.h>
#include <algorithm>
#include <atomic>

#include "voice-pitch-shifter.h"

namespace {
constexpr const char *kFilterId = "simple_pitchshift_filter";
constexpr const char *kSettingSemitone = "semitone";

struct PitchShiftFilter {
	obs_source_t *context;
	std::atomic<int> semitone{0};
	std::atomic<bool> resetRequested{false};
	VoicePitchShifter shifter;
};

void enabled_changed(void *data, calldata_t *)
{
	static_cast<PitchShiftFilter *>(data)->resetRequested.store(true, std::memory_order_release);
}

void update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<PitchShiftFilter *>(data);
	filter->semitone.store(int(std::clamp<int64_t>(obs_data_get_int(settings, kSettingSemitone), -12, 12)),
			       std::memory_order_relaxed);
	filter->resetRequested.store(true, std::memory_order_release);
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new PitchShiftFilter();
	filter->context = source;
	update(filter, settings);
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
	obs_data_set_default_int(settings, kSettingSemitone, 0);
}
obs_properties_t *properties(void *)
{
	auto *props = obs_properties_create();
	obs_properties_add_int_slider(props, kSettingSemitone, obs_module_text("SimplePitchShiftSemitone"), -12, 12, 1);
	return props;
}

obs_audio_data *process(void *data, obs_audio_data *audio)
{
	auto *filter = static_cast<PitchShiftFilter *>(data);
	auto *output = obs_get_audio();
	const auto rate = audio_output_get_sample_rate(output);
	if (audio_output_get_channels(output) != 2 || (rate != 44100 && rate != 48000)) {
		filter->shifter.reset();
		return audio;
	}
	if (filter->resetRequested.exchange(false, std::memory_order_acq_rel))
		filter->shifter.reset();
	const auto latency = filter->shifter.process(reinterpret_cast<float *>(audio->data[0]),
						     reinterpret_cast<float *>(audio->data[1]), audio->frames, rate,
						     filter->semitone.load(std::memory_order_relaxed));
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
