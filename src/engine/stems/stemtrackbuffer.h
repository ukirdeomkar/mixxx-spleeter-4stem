#pragma once

#include <vector>
#include <atomic>
#include <cstdint>
#include <algorithm>

namespace mixxx {

/// Whole-track storage for the four separated stems.
///
/// Frames are published progressively: the worker writes block after block and
/// `framesReady()` is the length of the contiguous prefix that is safe to read.
/// Frames past that prefix have not been separated yet and must fall back to the
/// original mix.
///
/// Memory: 4 stems * 2 channels * frames * 4 bytes, i.e. roughly 420 MB for a
/// five minute track. See docs/ai-stems-spleeter.md before raising this.
class StemTrackBuffer {
  public:
    StemTrackBuffer();
    ~StemTrackBuffer() = default;

    // Allocate storage for totalFrames
    void allocate(uint32_t totalFrames);

    // Clear buffer and reset frame count
    void clear();

    // Check if the given frame range has been separated and is ready
    bool isFrameReady(uint32_t frame) const;
    bool isRangeReady(uint32_t startFrame, uint32_t numFrames) const;

    uint32_t framesReady() const {
        return m_framesReady.load(std::memory_order_acquire);
    }

    uint32_t totalFrames() const {
        return m_totalFrames;
    }

    // Write a separated chunk into the buffer (vocals, drums, bass, other)
    void writeChunk(
            uint32_t startFrame,
            uint32_t numFrames,
            const float* voc_l, const float* voc_r,
            const float* drum_l, const float* drum_r,
            const float* bass_l, const float* bass_r,
            const float* other_l, const float* other_r);

    // Read separated stem samples for a range of frames
    void readFrames(
            uint32_t startFrame,
            uint32_t numFrames,
            float* voc_l, float* voc_r,
            float* drum_l, float* drum_r,
            float* bass_l, float* bass_r,
            float* other_l, float* other_r) const;

  private:
    uint32_t m_totalFrames{0};
    std::atomic<uint32_t> m_framesReady{0};

    std::vector<float> m_vocL;
    std::vector<float> m_vocR;
    std::vector<float> m_drumL;
    std::vector<float> m_drumR;
    std::vector<float> m_bassL;
    std::vector<float> m_bassR;
    std::vector<float> m_otherL;
    std::vector<float> m_otherR;
};

} // namespace mixxx
