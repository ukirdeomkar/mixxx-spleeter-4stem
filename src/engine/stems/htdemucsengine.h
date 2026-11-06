#pragma once

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Ort {
// ONNX Runtime declares these as structs; matching the tag avoids MSVC C4099.
struct Env;
struct Session;
struct MemoryInfo;
} // namespace Ort

namespace mixxx {

/// Offline, high quality 4-stem separator built on HT-Demucs v4 (single-file
/// ONNX export, 316 MB, MIT).
///
/// This engine is deliberately NOT used for real-time playback: it runs at
/// roughly 0.6x real-time on the low-power Intel Macs this fork targets (see
/// docs/todo-hq-demucs.md for the measurements). It is driven from the track
/// context menu ("Add high quality stems"), which writes the finished stems to
/// a folder the user picks; from then on the deck prefers those stems over the
/// fast Spleeter engine for that track.
///
/// Stem order used everywhere: 0 = vocals, 1 = drums, 2 = bass, 3 = other.
/// The model itself returns [drums, bass, other, vocals]; the mapping happens
/// inside separateTrack().
class HTDemucsEngine {
  public:
    static HTDemucsEngine& instance();

    static constexpr int kSampleRate = 44100;
    static constexpr int kStemCount = 4;
    // Segment geometry fixed by the ONNX export: one 7.8 s stereo segment.
    static constexpr int kSegmentSamples = 343980;
    static constexpr int kOverlapSamples = kSegmentSamples / 4; // 25 % overlap
    static constexpr int kStrideSamples = kSegmentSamples - kOverlapSamples;

    /// Receives finished stem frames in ascending order. The pointers stay valid
    /// only for the duration of the call; `numFrames` samples may be copied.
    using ChunkCallback = std::function<void(uint32_t startFrame,
            uint32_t numFrames,
            const float* voc_l,
            const float* voc_r,
            const float* drum_l,
            const float* drum_r,
            const float* bass_l,
            const float* bass_r,
            const float* other_l,
            const float* other_r)>;

    /// True if htdemucs.onnx is present in one of the model directories.
    bool isAvailable();

    /// Load the ONNX session. `customModelPath` overrides the search paths.
    bool initialize(const QString& customModelPath = QString());

    bool isInitialized() const;

    /// Separate a whole track with 25 % overlap-add.
    ///
    /// Frames are delivered progressively: after segment k lands, everything up
    /// to k * kStrideSamples is final and passed to `chunkCallback`. Peak extra
    /// memory is a couple of segments, not a second copy of the track.
    bool separateTrack(uint32_t totalFrames,
            const float* in_l,
            const float* in_r,
            const ChunkCallback& chunkCallback,
            std::function<void(double)> progressCallback = nullptr,
            std::atomic<bool>* pCancelFlag = nullptr);

    void finalize();

  private:
    HTDemucsEngine();
    ~HTDemucsEngine();

    /// Run one 7.8 s segment from m_input. Output is written to m_chunk as
    /// eight planes (vocL, vocR, drumL, drumR, bassL, bassR, otherL, otherR).
    bool separateSegment();

    std::unique_ptr<Ort::Env> m_pEnv;
    std::unique_ptr<Ort::Session> m_pSession;
    std::unique_ptr<Ort::MemoryInfo> m_pMemoryInfo;
    std::string m_inputName;
    std::string m_outputName;

    // Scratch, sized once in initialize().
    std::vector<float> m_input;  // 2 * kSegmentSamples
    std::vector<float> m_chunk[kStemCount * 2]; // kSegmentSamples each
    std::vector<float> m_acc[kStemCount * 2];   // kSegmentSamples each
    std::vector<float> m_weight;                // kSegmentSamples
    std::vector<float> m_window;                // kSegmentSamples

    bool m_bInitialized{false};
    mutable std::recursive_mutex m_mutex;
};

} // namespace mixxx
