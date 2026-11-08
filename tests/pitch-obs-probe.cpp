#include <obs.h>
#include <util/platform.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
struct Wave {
	uint32_t rate = 0;
	std::vector<float> samples;
};
uint16_t u16(const char *p)
{
	uint16_t v;
	std::memcpy(&v, p, 2);
	return v;
}
uint32_t u32(const char *p)
{
	uint32_t v;
	std::memcpy(&v, p, 4);
	return v;
}
Wave read_wave(const char *path)
{
	std::ifstream f(path, std::ios::binary);
	char header[12];
	f.read(header, 12);
	if (!f || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4))
		throw std::runtime_error("input must be RIFF WAVE");
	Wave w;
	bool valid = false;
	while (f.read(header, 8)) {
		uint32_t n = u32(header + 4);
		if (n > 512 * 1024 * 1024)
			throw std::runtime_error("oversized WAV chunk");
		std::vector<char> chunk(n);
		f.read(chunk.data(), n);
		if (!f)
			throw std::runtime_error("truncated WAV");
		if (!std::memcmp(header, "fmt ", 4) && n >= 16) {
			uint16_t format = u16(chunk.data());
			if (format == 65534 && n >= 40)
				format = u16(chunk.data() + 24);
			valid = format == 3 && u16(chunk.data() + 2) == 2 && u16(chunk.data() + 14) == 32;
			w.rate = u32(chunk.data() + 4);
		}
		if (!std::memcmp(header, "data", 4)) {
			if (n % 8)
				throw std::runtime_error("unaligned stereo WAV");
			w.samples.resize(n / 4);
			std::memcpy(w.samples.data(), chunk.data(), n);
		}
		if (n & 1)
			f.ignore();
	}
	if (!valid || (w.rate != 44100 && w.rate != 48000) || w.samples.empty())
		throw std::runtime_error("input must be 44100/48000 Hz stereo float32 WAV");
	for (float x : w.samples)
		if (!std::isfinite(x))
			throw std::runtime_error("nonfinite input");
	return w;
}
void write_wave(const std::filesystem::path &path, const Wave &w)
{
	std::ofstream f(path, std::ios::binary);
	auto put16 = [&](uint16_t v) {
		f.write(reinterpret_cast<const char *>(&v), 2);
	};
	auto put32 = [&](uint32_t v) {
		f.write(reinterpret_cast<const char *>(&v), 4);
	};
	uint32_t bytes = uint32_t(w.samples.size() * 4);
	f.write("RIFF", 4);
	put32(bytes + 36);
	f.write("WAVEfmt ", 8);
	put32(16);
	put16(3);
	put16(2);
	put32(w.rate);
	put32(w.rate * 8);
	put16(8);
	put16(32);
	f.write("data", 4);
	put32(bytes);
	f.write(reinterpret_cast<const char *>(w.samples.data()), bytes);
	if (!f)
		throw std::runtime_error("output WAV write failed");
}
struct Capture {
	Wave wave;
	uint64_t input_timestamp = 0, delay = 0;
	size_t calls = 0;
	bool timestamps = true, finite = true;
	void clear()
	{
		wave.samples.clear();
		calls = 0;
		timestamps = finite = true;
		delay = 0;
	}
};
void capture(void *opaque, obs_source_t *, const audio_data *audio, bool)
{
	auto &c = *static_cast<Capture *>(opaque);
	uint64_t delay = c.input_timestamp >= audio->timestamp ? c.input_timestamp - audio->timestamp : UINT64_MAX;
	if (!c.calls)
		c.delay = delay;
	c.timestamps &= delay == c.delay;
	++c.calls;
	for (uint32_t i = 0; i < audio->frames; ++i)
		for (int channel = 0; channel < 2; ++channel) {
			float x = reinterpret_cast<const float *>(audio->data[channel])[i];
			c.finite &= std::isfinite(x);
			c.wave.samples.push_back(x);
		}
}
struct Fixture {
	obs_source_t *source = nullptr, *filter = nullptr;
	Capture output;
	uint64_t frame = 0, base = os_gettime_ns();
	struct Options {
		double semitone = 0;
		int window_ms = 0; // 0 leaves the key unset so the filter default applies.
		double formant = 0, gain_db = 0;
		bool legacy_int_semitone = false; // v1.0.0 saved semitone as an integer.
	};
	Fixture(uint32_t rate, double semitone) : Fixture(rate, Options{semitone}) {}
	Fixture(uint32_t rate, const Options &o)
	{
		output.wave.rate = rate;
		source = obs_source_create_private("pitch_probe_input", "isolated probe", nullptr);
		auto *settings = obs_data_create();
		if (o.legacy_int_semitone)
			obs_data_set_int(settings, "semitone", int(o.semitone));
		else
			obs_data_set_double(settings, "semitone", o.semitone);
		if (o.window_ms)
			obs_data_set_int(settings, "window_ms", o.window_ms);
		if (!o.legacy_int_semitone) {
			obs_data_set_double(settings, "formant", o.formant);
			obs_data_set_double(settings, "gain", o.gain_db);
		}
		filter = obs_source_create_private("simple_pitchshift_filter", "pitch", settings);
		obs_data_release(settings);
		if (!source || !filter)
			throw std::runtime_error("source/filter creation failed");
		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_NONE);
		obs_source_set_audio_mixers(source, 0);
		obs_source_filter_add(source, filter);
		obs_source_add_audio_capture_callback(source, capture, &output);
	}
	~Fixture()
	{
		if (source) {
			obs_source_remove_audio_capture_callback(source, capture, &output);
			if (filter)
				obs_source_filter_remove(source, filter);
		}
		obs_source_release(filter);
		obs_source_release(source);
	}
	void send(const std::vector<float> &samples)
	{
		// Include blocks larger than the DSP scratch buffer and irregular boundaries.
		constexpr uint32_t sizes[] = {127, 1024, 2049, 511};
		size_t offset = 0, block = 0;
		std::array<std::vector<float>, 2> planar;
		while (offset < samples.size() / 2) {
			uint32_t n = uint32_t(std::min<size_t>(sizes[block++ % 4], samples.size() / 2 - offset));
			for (int ch = 0; ch < 2; ++ch) {
				planar[ch].resize(n);
				for (uint32_t i = 0; i < n; ++i)
					planar[ch][i] = samples[2 * (offset + i) + ch];
			}
			obs_source_audio a{};
			a.frames = n;
			a.samples_per_sec = output.wave.rate;
			a.speakers = SPEAKERS_STEREO;
			a.format = AUDIO_FORMAT_FLOAT_PLANAR;
			a.timestamp = base + frame * 1000000000ULL / output.wave.rate;
			output.input_timestamp = a.timestamp;
			for (int ch = 0; ch < 2; ++ch)
				a.data[ch] = reinterpret_cast<const uint8_t *>(planar[ch].data());
			obs_source_output_audio(source, &a);
			offset += n;
			frame += n;
		}
	}
};
double peak(const std::vector<float> &s)
{
	double p = 0;
	for (float x : s)
		p = std::max(p, std::abs(double(x)));
	return p;
}
double rms(const std::vector<float> &s)
{
	double sum = 0;
	for (float x : s)
		sum += double(x) * x;
	return std::sqrt(sum / std::max<size_t>(1, s.size()));
}
std::vector<float> tone(uint32_t rate)
{
	std::vector<float> s(rate * 4);
	for (size_t i = 0; i < s.size() / 2; ++i)
		s[i * 2] = s[i * 2 + 1] = float(0.2 * std::sin(2 * pi * 220 * i / rate));
	return s;
}
bool transitions(const Wave &input, const std::filesystem::path &out, std::ostream &report)
{
	Fixture f(input.rate, 3);
	// Two seconds of representative audio fill the delay line before each change.
	std::vector<float> clip(input.samples.begin(),
				input.samples.begin() + std::min<size_t>(input.samples.size(), input.rate * 4));
	std::vector<float> silence(input.rate * 2, 0);
	f.send(clip);
	const uint64_t shifted_delay = f.output.delay;
	bool passed = f.output.finite && f.output.timestamps && shifted_delay > 0 && shifted_delay != UINT64_MAX;
	const uint64_t ms = 1000000;
	double semitone = 3, formant = 0;
	int window_ms = 40;
	auto update = [&](double new_semitone, double new_formant, int new_window_ms) {
		semitone = new_semitone;
		formant = new_formant;
		window_ms = new_window_ms;
		auto *settings = obs_data_create();
		obs_data_set_double(settings, "semitone", semitone);
		obs_data_set_double(settings, "formant", formant);
		obs_data_set_int(settings, "window_ms", window_ms);
		obs_source_update(f.filter, settings);
		obs_data_release(settings);
		f.output.clear();
	};
	// silence: "exact" requires bit-exact silence (a reset or bypass), "tail" allows in-flight audio to finish
	// within the first half and requires silence afterwards (a parameter change without reset).
	enum class Expect { exact_input, exact_silence, tail_then_silence, audible };
	auto check = [&](const char *stage, const std::vector<float> &samples, Expect expect) {
		f.send(samples);
		auto *settings = obs_source_get_settings(f.filter);
		bool retained = obs_data_get_double(settings, "semitone") == semitone &&
				obs_data_get_double(settings, "formant") == formant &&
				obs_data_get_int(settings, "window_ms") == window_ms;
		obs_data_release(settings);
		const auto &got = f.output.wave.samples;
		bool full = got.size() == samples.size();
		bool matched = true;
		if (expect == Expect::exact_input || expect == Expect::exact_silence)
			matched = got == samples;
		if (expect == Expect::tail_then_silence)
			matched = full && peak(std::vector<float>(got.begin() + got.size() / 2, got.end())) == 0;
		if (expect == Expect::audible)
			matched = rms(got) > 0;
		bool bypass = semitone == 0 && formant == 0;
		bool timing = f.output.timestamps && f.output.delay == (bypass ? 0 : uint64_t(window_ms) * ms);
		bool ok = f.output.finite && full && matched && timing && retained;
		passed &= ok;
		report << "{\"stage\":\"" << stage << "\",\"semitone\":" << semitone << ",\"formant\":" << formant
		       << ",\"window_ms\":" << window_ms << ",\"passed\":" << (ok ? "true" : "false")
		       << ",\"finite\":" << f.output.finite << ",\"all_frames\":" << full
		       << ",\"expected_content\":" << matched << ",\"timestamp_delay_ns\":" << f.output.delay
		       << ",\"timestamp_expected\":" << timing << ",\"settings_updated\":" << retained
		       << ",\"peak\":" << peak(got) << '}';
	};
	report << '[';
	update(-3, 0, 40);
	check("plus3_to_minus3_no_reset_silence", silence, Expect::tail_then_silence);
	report << ',';
	f.output.clear();
	f.send(clip);
	update(0, 0, 40);
	check("minus3_to_zero_representative", clip, Expect::exact_input);
	report << ',';
	update(3, 0, 40);
	check("zero_to_plus3_silence", silence, Expect::exact_silence);
	report << ',';
	f.output.clear();
	check("resumed_plus3_representative", clip, Expect::audible);
	write_wave(out / "transition-resumed-plus3.wav", f.output.wave);
	report << ',';
	update(3, 0, 10);
	check("window_40_to_10_silence", silence, Expect::exact_silence);
	report << ',';
	f.output.clear();
	check("window_10_representative", clip, Expect::audible);
	write_wave(out / "transition-window10-plus3.wav", f.output.wave);
	report << ',';
	update(0, 0, 10);
	f.output.clear();
	f.send(clip);
	update(0, 3, 10);
	check("formant_zero_to_plus3_crossing", clip, Expect::audible);
	write_wave(out / "transition-formant-plus3.wav", f.output.wave);
	report << ',';
	update(0, 0, 10);
	check("formant_plus3_to_zero_crossing", clip, Expect::exact_input);
	report << ',';
	// Drag the pitch slider in 0.1 steps while audio flows; values change without resets.
	update(3, 0, 20);
	f.output.clear();
	f.send(std::vector<float>(clip.begin(), clip.begin() + input.rate / 5 * 2));
	bool drag_ok = f.output.finite;
	size_t drag_frames = 0, drag_sent = 0;
	for (int step = 1; step <= 20; ++step) {
		auto *settings = obs_data_create();
		obs_data_set_double(settings, "semitone", 3 + step * 0.1);
		obs_data_set_double(settings, "formant", 0);
		obs_data_set_int(settings, "window_ms", 20);
		obs_source_update(f.filter, settings);
		obs_data_release(settings);
		f.output.clear();
		std::vector<float> part(clip.begin() + (step - 1) * 1024 * 2, clip.begin() + step * 1024 * 2);
		f.send(part);
		drag_sent += part.size();
		drag_frames += f.output.wave.samples.size();
		drag_ok &= f.output.finite && f.output.timestamps && f.output.delay == 20 * ms;
	}
	drag_ok &= drag_frames == drag_sent;
	passed &= drag_ok;
	report << "{\"stage\":\"semitone_drag_3_to_5_step_0.1\",\"passed\":" << (drag_ok ? "true" : "false")
	       << ",\"all_frames\":" << (drag_frames == drag_sent) << '}';
	report << ']';
	return passed;
}
double frequency(const Wave &w)
{
	// Interpolated positive crossings on the settled second half are diagnostic only.
	std::vector<double> crossings;
	for (size_t i = w.samples.size() / 4 + 1; i < w.samples.size() / 2; ++i) {
		double a = w.samples[(i - 1) * 2], b = w.samples[i * 2];
		if (a <= 0 && b > 0)
			crossings.push_back(double(i - 1) - a / (b - a));
	}
	return crossings.size() > 1 ? (crossings.size() - 1) * w.rate / (crossings.back() - crossings.front()) : 0;
}
struct MixCapture {
	Wave wave;
	std::mutex mutex;
	std::vector<std::array<uint64_t, 3>> packets;
	bool finite = true, continuous = true;
};
void mix_capture(void *opaque, size_t, audio_data *a)
{
	auto &c = *static_cast<MixCapture *>(opaque);
	std::lock_guard<std::mutex> lock(c.mutex);
	if (!c.packets.empty()) {
		auto last = c.packets.back();
		int64_t error = int64_t(a->timestamp - last[0]) - int64_t(last[2] * 1000000000ULL / c.wave.rate);
		c.continuous &= std::abs(error) <= 1;
	}
	c.packets.push_back({a->timestamp, os_gettime_ns(), a->frames});
	for (uint32_t i = 0; i < a->frames; ++i)
		for (int ch = 0; ch < 2; ++ch) {
			float x = reinterpret_cast<const float *>(a->data[ch])[i];
			c.finite &= std::isfinite(x);
			c.wave.samples.push_back(x);
		}
}
bool final_mix(const Wave &input, const std::filesystem::path &out, bool restore, int semitone, const char *data,
	       const char *graphics)
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
	const std::string data_path = std::string(data) + "/";
	obs_add_data_path(data_path.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	obs_video_info vi{};
	vi.graphics_module = graphics;
	vi.fps_num = 30;
	vi.fps_den = 1;
	vi.base_width = vi.output_width = 64;
	vi.base_height = vi.output_height = 64;
	vi.output_format = VIDEO_FORMAT_RGBA;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_FULL;
	vi.scale_type = OBS_SCALE_BILINEAR;
	if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS)
		throw std::runtime_error("video clock init failed");
	Fixture f(input.rate, semitone);
	const auto settings_path = (out / "saved-source.json").string();
	if (restore) {
		obs_source_remove_audio_capture_callback(f.source, capture, &f.output);
		obs_source_filter_remove(f.source, f.filter);
		obs_source_release(f.filter);
		f.filter = nullptr;
		obs_source_release(f.source);
		auto *saved = obs_data_create_from_json_file(settings_path.c_str());
		f.source = saved ? obs_load_source(saved) : nullptr;
		obs_data_release(saved);
		if (!f.source)
			throw std::runtime_error("saved source load failed");
		f.filter = obs_source_get_filter_by_name(f.source, "pitch");
		if (!f.filter)
			throw std::runtime_error("saved filter missing");
		obs_source_add_audio_capture_callback(f.source, capture, &f.output);
	} else {
		auto *saved = obs_save_source(f.source);
		bool ok = obs_data_save_json(saved, settings_path.c_str());
		obs_data_release(saved);
		if (!ok)
			throw std::runtime_error("settings JSON save failed");
	}
	auto *settings = obs_source_get_settings(f.filter);
	bool retained = obs_data_get_int(settings, "semitone") == semitone;
	obs_data_release(settings);
	obs_source_set_monitoring_type(f.source, OBS_MONITORING_TYPE_NONE);
	obs_source_set_audio_mixers(f.source, 1);
	obs_set_output_source(0, f.source);
	MixCapture mixed;
	mixed.wave.rate = input.rate;
	audio_convert_info conversion{};
	conversion.samples_per_sec = input.rate;
	conversion.speakers = SPEAKERS_STEREO;
	conversion.format = AUDIO_FORMAT_FLOAT_PLANAR;
	obs_add_raw_audio_callback(0, &conversion, mix_capture, &mixed);
	// Lead and tail silence isolate source onset and allow the final mix to drain.
	std::vector<float> samples(input.rate, 0);
	samples.insert(samples.end(), input.samples.begin(), input.samples.end());
	samples.resize(samples.size() + input.rate * 2, 0);
	std::ofstream sent(out / (restore ? "restore-input.csv" : "save-input.csv"));
	sent << "frame,timestamp_ns,submit_wall_ns,frames\n";
	f.base = os_gettime_ns();
	const uint32_t block = input.rate / 100;
	std::array<std::vector<float>, 2> planar;
	uint64_t input_onset_wall = 0;
	for (size_t offset = 0; offset < samples.size() / 2; offset += block) {
		uint32_t n = uint32_t(std::min<size_t>(block, samples.size() / 2 - offset));
		for (int ch = 0; ch < 2; ++ch) {
			planar[ch].resize(n);
			for (uint32_t i = 0; i < n; ++i)
				planar[ch][i] = samples[2 * (offset + i) + ch];
		}
		obs_source_audio a{};
		a.frames = n;
		a.samples_per_sec = input.rate;
		a.speakers = SPEAKERS_STEREO;
		a.format = AUDIO_FORMAT_FLOAT_PLANAR;
		a.timestamp = f.base + offset * 1000000000ULL / input.rate;
		os_sleepto_ns(a.timestamp + uint64_t(n) * 1000000000ULL / input.rate);
		f.output.input_timestamp = a.timestamp;
		for (int ch = 0; ch < 2; ++ch)
			a.data[ch] = reinterpret_cast<const uint8_t *>(planar[ch].data());
		uint64_t wall = os_gettime_ns();
		if (!input_onset_wall && (peak(planar[0]) > 1e-7 || peak(planar[1]) > 1e-7))
			input_onset_wall = wall;
		sent << offset << ',' << a.timestamp << ',' << wall << ',' << n << '\n';
		obs_source_output_audio(f.source, &a);
	}
	os_sleep_ms(300);
	obs_remove_raw_audio_callback(0, mix_capture, &mixed);
	obs_set_output_source(0, nullptr);
	std::lock_guard<std::mutex> lock(mixed.mutex);
	auto onset = [](const std::vector<float> &s) {
		size_t i = 0;
		while (i < s.size() / 2 && std::abs(s[i * 2]) <= 1e-7 && std::abs(s[i * 2 + 1]) <= 1e-7)
			++i;
		return i;
	};
	size_t source_onset = onset(f.output.wave.samples), mix_onset = onset(mixed.wave.samples);
	size_t compared = 0;
	double max_error = 0, squared_error = 0;
	for (size_t a = source_onset * 2, b = mix_onset * 2;
	     a < f.output.wave.samples.size() && b < mixed.wave.samples.size(); ++a, ++b) {
		double error = std::abs(double(f.output.wave.samples[a]) - mixed.wave.samples[b]);
		max_error = std::max(max_error, error);
		squared_error += error * error;
		++compared;
	}
	uint64_t onset_wall = 0, onset_timestamp = 0;
	size_t frame = 0;
	std::ofstream packets(out / (restore ? "restore-mix.csv" : "save-mix.csv"));
	packets << "frame,timestamp_ns,receive_wall_ns,frames\n";
	for (auto p : mixed.packets) {
		packets << frame << ',' << p[0] << ',' << p[1] << ',' << p[2] << '\n';
		if (mix_onset >= frame && mix_onset < frame + p[2]) {
			onset_wall = p[1];
			onset_timestamp = p[0] + (mix_onset - frame) * 1000000000ULL / input.rate;
		}
		frame += size_t(p[2]);
	}
	bool passed = retained && mixed.finite && mixed.continuous && f.output.timestamps &&
		      rms(mixed.wave.samples) > 0 && compared >= input.samples.size() && max_error < 1e-5;
	const std::string prefix = restore ? "restore" : "save";
	write_wave(out / (prefix + "-final-mix.wav"), mixed.wave);
	write_wave(out / (prefix + "-filter-output.wav"), f.output.wave);
	std::ofstream report(out / (prefix + "-result.json"));
	report << "{\"passed\":" << (passed ? "true" : "false") << ",\"rate\":" << input.rate
	       << ",\"semitone\":" << semitone << ",\"restored_from_disk\":" << restore
	       << ",\"settings_retained\":" << retained << ",\"finite\":" << mixed.finite
	       << ",\"mix_timestamps_continuous\":" << mixed.continuous << ",\"mix_packets\":" << mixed.packets.size()
	       << ",\"mix_frames\":" << mixed.wave.samples.size() / 2 << ",\"rms\":" << rms(mixed.wave.samples)
	       << ",\"compared_samples\":" << compared << ",\"max_pcm_error\":" << max_error
	       << ",\"pcm_rmse\":" << std::sqrt(squared_error / std::max<size_t>(1, compared))
	       << ",\"filter_timestamp_subtraction_ns\":" << f.output.delay
	       << ",\"source_onset_frame\":" << source_onset << ",\"mix_onset_frame\":" << mix_onset
	       << ",\"input_onset_submit_wall_ns\":" << input_onset_wall
	       << ",\"mix_onset_receive_wall_ns\":" << onset_wall
	       << ",\"onset_block_wall_delta_ns\":" << int64_t(onset_wall - input_onset_wall)
	       << ",\"mix_onset_timestamp_ns\":" << onset_timestamp << ",\"filter_onset_timestamp_ns\":"
	       << f.base + source_onset * 1000000000ULL / input.rate - f.output.delay
	       << ",\"note\":\"Wall delta is first active input block submission to first active final-mix callback; includes block quantization and OBS buffering, excludes audio device. No video content or AV sync assertion.\"}\n";
	return passed;
}
// Saves the source with its filter and returns the filter settings loaded back from that save.
obs_data_t *restored_settings(obs_source_t *source)
{
	auto *saved = obs_save_source(source);
	auto *restored = obs_load_source(saved);
	obs_data_release(saved);
	auto *filter = restored ? obs_source_get_filter_by_name(restored, "pitch") : nullptr;
	auto *settings = filter ? obs_source_get_settings(filter) : nullptr;
	obs_source_release(filter);
	obs_source_release(restored);
	return settings;
}
double max_scaled_error(const std::vector<float> &got, const std::vector<float> &reference, double scale)
{
	if (got.size() != reference.size())
		return INFINITY;
	double error = 0;
	for (size_t i = 0; i < got.size(); ++i)
		error = std::max(error, std::abs(double(got[i]) - scale * reference[i]));
	return error;
}
bool settings_cases(const Wave &input, const std::filesystem::path &out, std::ostream &report)
{
	const uint64_t ms = 1000000;
	const double input_seconds = double(input.samples.size() / 2) / input.rate;
	bool passed = true, first = true;
	auto emit = [&](const std::string &name, bool ok, const std::string &fields) {
		passed &= ok;
		report << (first ? "" : ",") << "{\"case\":\"" << name << "\",\"passed\":" << (ok ? "true" : "false")
		       << fields << '}';
		first = false;
	};
	for (int window_ms : {10, 20, 40}) {
		Fixture f(input.rate, Fixture::Options{3, window_ms});
		uint64_t start = os_gettime_ns();
		f.send(input.samples);
		double realtime_factor = double(os_gettime_ns() - start) / 1e9 / input_seconds;
		auto *settings = restored_settings(f.source);
		bool retained = settings && obs_data_get_int(settings, "window_ms") == window_ms;
		obs_data_release(settings);
		bool full = f.output.wave.samples.size() == input.samples.size();
		bool timing = f.output.timestamps && f.output.delay == uint64_t(window_ms) * ms;
		write_wave(out / ("window-" + std::to_string(window_ms) + "-plus3.wav"), f.output.wave);
		emit("window_" + std::to_string(window_ms) + "_plus3",
		     f.output.finite && full && timing && retained && rms(f.output.wave.samples) > 0,
		     ",\"timestamp_delay_ns\":" + std::to_string(f.output.delay) +
			     ",\"settings_restored\":" + std::to_string(retained) +
			     ",\"send_seconds_per_input_second\":" + std::to_string(realtime_factor));
	}
	{
		Fixture::Options legacy{3};
		legacy.legacy_int_semitone = true;
		Fixture f(input.rate, legacy);
		f.send(input.samples);
		auto *settings = obs_source_get_settings(f.filter);
		bool read = obs_data_get_double(settings, "semitone") == 3 &&
			    obs_data_get_int(settings, "window_ms") == 40 &&
			    obs_data_get_double(settings, "formant") == 0 && obs_data_get_double(settings, "gain") == 0;
		obs_data_release(settings);
		bool timing = f.output.timestamps && f.output.delay == 40 * ms;
		emit("v100_integer_semitone_settings", f.output.finite && read && timing,
		     ",\"defaults_and_value_read\":" + std::to_string(read) +
			     ",\"timestamp_delay_ns\":" + std::to_string(f.output.delay));
	}
	{
		Fixture f(input.rate, 2.5);
		f.send(tone(input.rate));
		double hz = frequency(f.output.wave);
		double cents = hz > 0 ? 1200 * std::log2(hz / (220 * std::pow(2.0, 2.5 / 12.0))) : 0;
		auto *settings = restored_settings(f.source);
		bool retained = settings && obs_data_get_double(settings, "semitone") == 2.5;
		obs_data_release(settings);
		emit("fractional_semitone_2_5", f.output.finite && retained && hz > 0,
		     ",\"settings_restored\":" + std::to_string(retained) + ",\"tone_hz\":" + std::to_string(hz) +
			     ",\"tone_error_cents\":" + std::to_string(cents));
	}
	{
		Fixture::Options o{0, 40, 3};
		Fixture f(input.rate, o);
		f.send(input.samples);
		auto *settings = restored_settings(f.source);
		bool retained = settings && obs_data_get_double(settings, "formant") == 3;
		obs_data_release(settings);
		bool processed = f.output.wave.samples != input.samples && rms(f.output.wave.samples) > 0;
		bool timing = f.output.timestamps && f.output.delay == 40 * ms;
		write_wave(out / "formant-plus3-pitch0.wav", f.output.wave);
		emit("formant_plus3_at_semitone_0", f.output.finite && processed && timing && retained,
		     ",\"processed\":" + std::to_string(processed) + ",\"settings_restored\":" +
			     std::to_string(retained) + ",\"timestamp_delay_ns\":" + std::to_string(f.output.delay));
	}
	const double six_db = 20 * std::log10(2.0);
	for (double gain_db : {six_db, -six_db}) {
		Fixture::Options o{0};
		o.gain_db = gain_db;
		Fixture f(input.rate, o);
		f.send(input.samples);
		double error = max_scaled_error(f.output.wave.samples, input.samples, std::pow(10.0, gain_db / 20));
		auto *settings = restored_settings(f.source);
		bool retained = settings && obs_data_get_double(settings, "gain") == gain_db;
		obs_data_release(settings);
		bool timing = f.output.timestamps && f.output.delay == 0;
		emit(gain_db > 0 ? "gain_plus6db_bypass" : "gain_minus6db_bypass",
		     f.output.finite && error < 1e-5 && timing && retained,
		     ",\"max_error\":" + std::to_string(error) + ",\"settings_restored\":" + std::to_string(retained));
	}
	{
		Fixture plain(input.rate, 3.0);
		plain.send(input.samples);
		Fixture::Options o{3};
		o.gain_db = six_db;
		Fixture gained(input.rate, o);
		gained.send(input.samples);
		double error = max_scaled_error(gained.output.wave.samples, plain.output.wave.samples, 2.0);
		emit("gain_plus6db_after_pitch", gained.output.finite && error < 1e-4,
		     ",\"max_error_vs_twice_ungained\":" + std::to_string(error));
	}
	return passed;
}
} // namespace
int main(int argc, char **argv)
{
	bool mix_mode = argc == 9 && (std::strcmp(argv[5], "--final-mix-save") == 0 ||
				      std::strcmp(argv[5], "--final-mix-restore") == 0);
	if (!mix_mode && argc != 5 && !(argc == 6 && std::strcmp(argv[5], "--transitions-only") == 0)) {
		std::cerr
			<< "Usage: pitch-obs-probe plugin.dll plugin-data input-f32-stereo.wav output-directory [--transitions-only | --final-mix-save SEMITONE LIBOBS_DATA GRAPHICS_DLL | --final-mix-restore SEMITONE LIBOBS_DATA GRAPHICS_DLL]\n";
		return 2;
	}
	bool started = false;
	try {
		Wave input = read_wave(argv[3]);
		std::filesystem::path out(argv[4]);
		std::filesystem::create_directories(out);
		if (!obs_startup("en-US", nullptr, nullptr))
			throw std::runtime_error("obs_startup failed");
		started = true;
		obs_audio_info ai{input.rate, SPEAKERS_STEREO};
		if (!obs_reset_audio(&ai))
			throw std::runtime_error("obs_reset_audio failed");
		obs_source_info info{};
		info.id = "pitch_probe_input";
		info.type = OBS_SOURCE_TYPE_INPUT;
		info.output_flags = OBS_SOURCE_AUDIO;
		info.get_name = [](void *) {
			return "Isolated pitch probe";
		};
		info.create = [](obs_data_t *, obs_source_t *s) -> void * {
			return s;
		};
		info.destroy = [](void *) {
		};
		obs_register_source(&info);
		obs_module_t *module = nullptr;
		if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module))
			throw std::runtime_error("product module load failed");
		int count = 0;
		const char *id = nullptr;
		for (size_t i = 0; obs_enum_filter_types(i, &id); ++i)
			count += std::strcmp(id, "simple_pitchshift_filter") == 0;
		bool passed = count == 1;
		if (mix_mode) {
			passed &= final_mix(input, out, std::strcmp(argv[5], "--final-mix-restore") == 0,
					    std::stoi(argv[6]), argv[7], argv[8]);
			obs_shutdown();
			return passed ? 0 : 1;
		}
		std::ofstream report(out / "result.json");
		report << "{\"obs\":\"" << obs_get_version_string() << "\",\"rate\":" << input.rate
		       << ",\"registrations\":" << count << ",\"cases\":[";
		bool first = true;
		if (argc == 5)
			for (int semitone : {-12, -3, 0, 3, 12}) {
				Fixture f(input.rate, semitone);
				f.send(input.samples);
				bool bypass = semitone != 0 || f.output.wave.samples == input.samples;
				bool timestamps = f.output.timestamps &&
						  (semitone == 0 ? f.output.delay == 0
								 : f.output.delay > 0 && f.output.delay != UINT64_MAX);
				bool full = f.output.wave.samples.size() == input.samples.size();
				auto *saved = obs_save_source(f.source);
				auto *restored = obs_load_source(saved);
				obs_data_release(saved);
				auto *restored_filter = restored ? obs_source_get_filter_by_name(restored, "pitch")
								 : nullptr;
				auto *settings = restored_filter ? obs_source_get_settings(restored_filter) : nullptr;
				bool retained = settings && obs_data_get_double(settings, "semitone") == semitone;
				obs_data_release(settings);
				obs_source_release(restored_filter);
				obs_source_release(restored);
				write_wave(out / ("pitch-" + std::to_string(semitone) + ".wav"), f.output.wave);
				double representative_peak = peak(f.output.wave.samples),
				       representative_rms = rms(f.output.wave.samples);
				bool finite = f.output.finite;
				uint64_t delay = f.output.delay;
				f.output.clear();
				obs_source_set_enabled(f.filter, false);
				std::vector<float> silence(input.rate * 2, 0);
				f.send(silence);
				bool disabled_silent = peak(f.output.wave.samples) == 0;
				f.output.clear();
				obs_source_set_enabled(f.filter, true);
				f.send(silence);
				double stale_peak = peak(f.output.wave.samples);
				bool no_stale = f.output.finite && stale_peak == 0;
				Fixture diagnostic(input.rate, semitone);
				diagnostic.send(tone(input.rate));
				double hz = frequency(diagnostic.output.wave);
				double cents = hz > 0 ? 1200 * std::log2(hz / (220 * std::pow(2.0, semitone / 12.0)))
						      : 0;
				bool ok = finite && full && bypass && timestamps && retained && disabled_silent &&
					  no_stale;
				passed &= ok;
				if (!first)
					report << ',';
				first = false;
				report << "{\"semitone\":" << semitone << ",\"passed\":" << (ok ? "true" : "false")
				       << ",\"finite\":" << finite << ",\"all_frames\":" << full
				       << ",\"zero_bypass\":" << bypass << ",\"timestamps_consistent\":" << timestamps
				       << ",\"timestamp_delay_ns\":" << delay << ",\"settings_restored\":" << retained
				       << ",\"peak\":" << representative_peak << ",\"rms\":" << representative_rms
				       << ",\"disabled_silent\":" << disabled_silent
				       << ",\"reenable_silence_peak\":" << stale_peak << ",\"tone_hz\":" << hz
				       << ",\"tone_error_cents\":" << cents << '}';
			}
		if (argc == 5) {
			report << "],\"settings\":[";
			passed &= settings_cases(input, out, report);
		}
		report << "],\"transitions\":";
		passed &= transitions(input, out, report);
		report << ",\"passed\":" << (passed ? "true" : "false")
		       << ",\"quality_note\":\"Tone measurements are diagnostic; representative WAV listening review remains required.\"}\n";
		report.close();
		if (!report)
			throw std::runtime_error("report write failed");
		obs_shutdown();
		return passed ? 0 : 1;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		if (started)
			obs_shutdown();
		return 2;
	}
}
