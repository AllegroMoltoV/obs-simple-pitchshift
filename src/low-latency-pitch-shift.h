#pragma once

#include <cstdint>
#include <vector>

class LowLatencyPitchShift final {
public:
	void Prepare(uint32_t sample_rate);
	void Reset();
	void ProcessBlock(const float *in, float *out, uint32_t frames, double pitch_factor);

private:
	float ReadTap(double phase01, double pitch_factor) const;

	std::vector<float> buf_;
	int write_pos_ = 0;
	double phase_ = 0.0;

	int base_delay_samples_ = 256;
	int range_samples_ = 128;
};
