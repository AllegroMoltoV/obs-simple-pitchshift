#include "low-latency-pitch-shift.h"

#include <algorithm>
#include <cmath>

namespace {

// レイテンシ優先パラメータ (44.1 kHz 前提での目安は本文参照)
constexpr int kBaseDelaySamples = 256;
constexpr int kRangeSamples = 128;
constexpr int kDelayLineLength = 1024;

inline double clamp_double(double v, double lo, double hi)
{
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

inline double frac(double x)
{
	return x - std::floor(x);
}

inline float tri(double phase01)
{
	// 0..1..0 の三角窓 (クロスフェード用)
	const double f = frac(phase01);
	const double t = 1.0 - std::fabs(2.0 * f - 1.0);
	return (float)clamp_double(t, 0.0, 1.0);
}

} // namespace

void LowLatencyPitchShift::Prepare()
{
	buf_.assign(kDelayLineLength, 0.0f);
	Reset();
}

void LowLatencyPitchShift::Reset()
{
	std::fill(buf_.begin(), buf_.end(), 0.0f);
	write_pos_ = 0;
	phase_ = 0.0;
}

void LowLatencyPitchShift::ProcessBlock(const float *in, float *out, uint32_t frames, double pitch_factor)
{
	// pitch_factor = 2^(semitone/12)
	// 変調ディレイ方式の近似で、低遅延を優先する。
	const double slope = 1.0 - pitch_factor; // D'[n] (samples / sample)
	const double phase_step = std::fabs(slope) / (double)kRangeSamples;

	for (uint32_t i = 0; i < frames; i++) {
		buf_[write_pos_] = in[i];
		write_pos_ = (write_pos_ + 1) % (int)buf_.size();

		const double p1 = phase_;
		const double p2 = phase_ + 0.5;

		const float w1 = tri(p1);
		const float w2 = tri(p2);

		const float s1 = ReadTap(p1, pitch_factor);
		const float s2 = ReadTap(p2, pitch_factor);

		out[i] = s1 * w1 + s2 * w2;

		phase_ += phase_step;
		phase_ = frac(phase_);
	}
}

float LowLatencyPitchShift::ReadTap(double phase01, double pitch_factor) const
{
	const double f = frac(phase01);

	double delay = 0.0;
	if (pitch_factor >= 1.0) {
		delay = (double)kBaseDelaySamples + (double)kRangeSamples * (1.0 - f);
	} else {
		delay = (double)kBaseDelaySamples + (double)kRangeSamples * f;
	}

	const double len = (double)buf_.size();
	double pos = (double)write_pos_ - delay;
	pos = std::fmod(pos + len, len);

	const int i0 = (int)pos;
	const int i1 = (i0 + 1) % (int)buf_.size();
	const double frac01 = pos - (double)i0;

	const float a = buf_[i0];
	const float b = buf_[i1];
	return (float)((1.0 - frac01) * (double)a + frac01 * (double)b);
}
