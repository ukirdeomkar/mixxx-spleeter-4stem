#include "engine/stems/spleeterstft.h"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace mixxx {
namespace spleeter {

namespace {

// FFTW plan creation is not thread safe, even for independent plans.
std::mutex s_planMutex;

// Per-frame weighted overlap-add normalisation floor. Mirrors the reference
// implementation (`wsum > 1e-8`), which only matters on a zero-length ramp.
constexpr float kWeightEpsilon = 1e-8f;

} // namespace

struct StftProcessor::Impl {
    Impl() {
        // Periodic Hann window, identical to np.hanning(kFftSize + 1)[:-1].
        window.resize(kFftSize);
        for (int i = 0; i < kFftSize; ++i) {
            window[i] = static_cast<float>(
                    0.5 - 0.5 * std::cos(2.0 * M_PI * i / static_cast<double>(kFftSize)));
        }

        time.assign(kFftSize, 0.0f);
        spectrum.assign(kBins * 2, 0.0f);

        std::lock_guard<std::mutex> lock(s_planMutex);
        // FFTW_ESTIMATE plans work for any alignment, but claiming FFTW_UNALIGNED
        // keeps us safe if a caller hands us an oddly aligned buffer.
        planForward = fftwf_plan_dft_r2c_1d(kFftSize,
                time.data(),
                reinterpret_cast<fftwf_complex*>(spectrum.data()),
                FFTW_ESTIMATE | FFTW_UNALIGNED);
        planInverse = fftwf_plan_dft_c2r_1d(kFftSize,
                reinterpret_cast<fftwf_complex*>(spectrum.data()),
                time.data(),
                FFTW_ESTIMATE | FFTW_UNALIGNED);
    }

    ~Impl() {
        if (planForward) {
            fftwf_destroy_plan(planForward);
        }
        if (planInverse) {
            fftwf_destroy_plan(planInverse);
        }
    }

    std::vector<float> window;
    std::vector<float> time;
    std::vector<float> spectrum; // 2 * kBins, re/im interleaved
    fftwf_plan planForward{nullptr};
    fftwf_plan planInverse{nullptr};
};

StftProcessor::StftProcessor()
        : m_pImpl(std::make_unique<Impl>()) {
}

StftProcessor::~StftProcessor() = default;

void StftProcessor::forward(const float* signal,
        int numSamples,
        int firstFrame,
        int numFrames,
        float* outReIm) const {
    if (!signal || !outReIm || numFrames <= 0) {
        return;
    }

    Impl& impl = *m_pImpl;

    for (int f = 0; f < numFrames; ++f) {
        const int frame = firstFrame + f;
        // Start of this frame in signal coordinates.
        const int signalStart = frame * kHopSize - kPad;

        for (int n = 0; n < kFftSize; ++n) {
            const int sampleIndex = signalStart + n;
            float sample = 0.0f;
            if (sampleIndex >= 0 && sampleIndex < numSamples) {
                sample = signal[sampleIndex];
            }
            impl.time[n] = sample * impl.window[n];
        }

        fftwf_execute(impl.planForward);

        std::memcpy(outReIm + static_cast<size_t>(f) * kBins * 2,
                impl.spectrum.data(),
                sizeof(float) * kBins * 2);
    }
}

void StftProcessor::inverse(const float* inReIm,
        int numFrames,
        float* out) const {
    if (!inReIm || !out || numFrames <= 0) {
        return;
    }

    Impl& impl = *m_pImpl;

    // Frames extend past the last frame start, exactly like the reference
    // `total = (frames - 1) * hop + fftSize`. Only the first numFrames * hop
    // samples are returned; the tail belongs to the following block.
    const size_t outLength = static_cast<size_t>(numFrames) * kHopSize;
    const size_t overlapLength = outLength + (kFftSize - kHopSize);
    std::vector<float> accumulated(overlapLength, 0.0f);
    std::vector<float> weights(overlapLength, 0.0f);

    const float invFftSize = 1.0f / static_cast<float>(kFftSize);

    for (int f = 0; f < numFrames; ++f) {
        std::memcpy(impl.spectrum.data(),
                inReIm + static_cast<size_t>(f) * kBins * 2,
                sizeof(float) * kBins * 2);

        fftwf_execute(impl.planInverse);

        // FFTW's c2r is unnormalised by kFftSize compared to numpy's irfft.
        float* dest = accumulated.data() + static_cast<size_t>(f) * kHopSize;
        float* weight = weights.data() + static_cast<size_t>(f) * kHopSize;
        for (int n = 0; n < kFftSize; ++n) {
            const float w = impl.window[n];
            dest[n] += impl.time[n] * invFftSize * w;
            weight[n] += w * w;
        }
    }

    for (size_t i = 0; i < outLength; ++i) {
        const float w = weights[i];
        out[i] = (w > kWeightEpsilon) ? accumulated[i] / w : 0.0f;
    }
}

} // namespace spleeter
} // namespace mixxx
