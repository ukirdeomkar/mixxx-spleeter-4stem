#pragma once

#include <memory>
#include <vector>

namespace mixxx {
namespace spleeter {

// Audio / STFT contract of the Spleeter 4-stem ONNX models.
// See docs/ai-stems-spleeter.md.
inline constexpr int kSampleRate = 44100;
inline constexpr int kFftSize = 4096;
inline constexpr int kHopSize = 1024;
inline constexpr int kPad = kFftSize - kHopSize; // 3072
inline constexpr int kModelBins = 1024;          // bins fed to / returned by the net
inline constexpr int kBins = kFftSize / 2 + 1;   // 2049 rfft bins
inline constexpr int kFramesPerSplit = 512;      // fixed time axis of the ONNX graph

// Number of STFT frames covering a signal of numSamples samples.
// Mirrors the reference: n = ceil((kPad + numSamples) / kHopSize).
inline int frameCount(int numSamples) {
    if (numSamples <= 0) {
        return 0;
    }
    return (kPad + numSamples + kHopSize - 1) / kHopSize;
}

// A model block covers kFramesPerSplit frames but only yields
// kFramesPerSplit - (kFftSize / kHopSize - 1) = 509 frames worth of samples,
// because the outermost frames also contribute to neighbouring samples.
inline constexpr int kFramesPerBlock = kFramesPerSplit - (kFftSize / kHopSize - 1);
inline constexpr int kSamplesPerBlock = kFramesPerBlock * kHopSize; // 521216

// Number of model blocks needed for numSamples samples.
inline int blockCount(int numSamples) {
    const int frames = frameCount(numSamples);
    if (frames == 0) {
        return 0;
    }
    return (frames + kFramesPerBlock - 1) / kFramesPerBlock;
}

// Frame index of the first STFT frame of block blockIndex.
inline int blockFirstFrame(int blockIndex) {
    return blockIndex * kFramesPerBlock;
}

class StftProcessor {
  public:
    StftProcessor();
    ~StftProcessor();
    StftProcessor(const StftProcessor&) = delete;
    StftProcessor& operator=(const StftProcessor&) = delete;

    // Compute the complex spectra of frames [firstFrame, firstFrame + numFrames).
    // outReIm must hold numFrames * kBins * 2 floats, interleaved re/im.
    // Signal samples outside [0, numSamples) are treated as zero, and frames are
    // positioned in padded coordinates: frame t starts at padded sample t * kHopSize,
    // i.e. at signal sample t * kHopSize - kPad.
    void forward(const float* signal,
            int numSamples,
            int firstFrame,
            int numFrames,
            float* outReIm) const;

    // Weighted overlap-add inverse of frames [firstFrame, firstFrame + numFrames).
    // inReIm holds numFrames * kBins * 2 floats (same layout as forward()).
    // out receives numFrames * kHopSize samples, which correspond to padded
    // samples [firstFrame * kHopSize, (firstFrame + numFrames) * kHopSize).
    void inverse(const float* inReIm,
            int numFrames,
            float* out) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_pImpl;
};

} // namespace spleeter
} // namespace mixxx
