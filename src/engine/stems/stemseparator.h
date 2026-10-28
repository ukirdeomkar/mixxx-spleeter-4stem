#pragma once

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "engine/stems/spleeterstft.h"

namespace Ort {
// ONNX Runtime declares these as structs; matching the tag avoids MSVC C4099.
struct Env;
struct Session;
struct MemoryInfo;
} // namespace Ort

namespace mixxx {

/// Real-time-capable 4-stem separator built on Deezer's Spleeter 4stems
/// (exported to ONNX), replacing the HTDemucs engine of the upstream fork.
///
/// The models work on magnitudes only, so the STFT / ratio mask / iSTFT
/// frontend lives here (see spleeterstft.h for the contract).
class SpleeterEngine {
  public:
    static SpleeterEngine& instance();

    static constexpr int kSampleRate = spleeter::kSampleRate;
    static constexpr int kStemCount = 4;
    static constexpr int kSamplesPerBlock = spleeter::kSamplesPerBlock;
    static constexpr int kFramesPerSplit = spleeter::kFramesPerSplit;

    // Stem order used everywhere: 0 = vocals, 1 = drums, 2 = bass, 3 = other.
    enum StemIndex {
        kVocals = 0,
        kDrums = 1,
        kBass = 2,
        kOther = 3,
    };

    // True if all four ONNX models are present on disk.
    bool isAvailable();

    /// Load the four sessions. `customModelDir` overrides the search paths.
    bool initialize(const QString& customModelDir = QString());

    bool isInitialized() const;

    /// Total number of model blocks needed to cover `totalFrames` samples.
    static int blockCount(uint32_t totalFrames);

    /// Samples produced by `blockIndex` (the last block may be short).
    static int blockSampleCount(uint32_t totalFrames, int blockIndex);

    /// Separate one block. Output pointers receive blockSampleCount() samples
    /// starting at relative offset 0; they must have room for kSamplesPerBlock.
    /// Callers are expected to walk blocks in order, which is what makes the
    /// separation progressive.
    bool separateBlock(const float* in_l,
            const float* in_r,
            uint32_t totalFrames,
            int blockIndex,
            float* voc_l, float* voc_r,
            float* drum_l, float* drum_r,
            float* bass_l, float* bass_r,
            float* other_l, float* other_r,
            std::atomic<bool>* pCancelFlag = nullptr);

    /// Convenience whole-track separation (used by tests, not by playback).
    bool separateTrack(uint32_t totalFrames,
            const float* in_l,
            const float* in_r,
            float* voc_l, float* voc_r,
            float* drum_l, float* drum_r,
            float* bass_l, float* bass_r,
            float* other_l, float* other_r,
            std::function<void(double)> progressCallback = nullptr,
            std::atomic<bool>* pCancelFlag = nullptr);

    void finalize();

  private:
    SpleeterEngine();
    ~SpleeterEngine();

    bool locateModels(QString modelPaths[kStemCount]) const;

    std::unique_ptr<Ort::Env> m_pEnv;
    std::unique_ptr<Ort::Session> m_pSessions[kStemCount];
    std::unique_ptr<Ort::MemoryInfo> m_pMemoryInfo;
    std::unique_ptr<spleeter::StftProcessor> m_pStft;

    // Scratch, sized once in initialize().
    std::vector<float> m_specL;      // kFramesPerSplit * kBins * 2
    std::vector<float> m_specR;      // kFramesPerSplit * kBins * 2
    std::vector<float> m_netInput;   // 2 * kFramesPerSplit * kModelBins
    std::vector<float> m_estimates[kStemCount]; // 2 * kFramesPerSplit * kModelBins
    std::vector<float> m_denominator;           // 2 * kFramesPerSplit * kModelBins
    std::vector<float> m_mask;                  // kFramesPerSplit * kModelBins
    std::vector<float> m_maskedSpec;            // kFramesPerSplit * kBins * 2
    std::vector<float> m_ola;                   // kFramesPerSplit * kHopSize + tail

    bool m_bInitialized{false};
    mutable std::recursive_mutex m_mutex;
};

} // namespace mixxx
