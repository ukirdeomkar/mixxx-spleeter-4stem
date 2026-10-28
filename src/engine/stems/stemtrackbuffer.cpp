#include "engine/stems/stemtrackbuffer.h"

#include <cstring>

namespace mixxx {

StemTrackBuffer::StemTrackBuffer() = default;

void StemTrackBuffer::allocate(uint32_t totalFrames) {
    m_framesReady.store(0, std::memory_order_release);
    m_totalFrames = totalFrames;

    m_vocL.assign(totalFrames, 0.0f);
    m_vocR.assign(totalFrames, 0.0f);
    m_drumL.assign(totalFrames, 0.0f);
    m_drumR.assign(totalFrames, 0.0f);
    m_bassL.assign(totalFrames, 0.0f);
    m_bassR.assign(totalFrames, 0.0f);
    m_otherL.assign(totalFrames, 0.0f);
    m_otherR.assign(totalFrames, 0.0f);
}

void StemTrackBuffer::clear() {
    m_framesReady.store(0, std::memory_order_release);
    m_totalFrames = 0;
    m_vocL.clear();
    m_vocR.clear();
    m_drumL.clear();
    m_drumR.clear();
    m_bassL.clear();
    m_bassR.clear();
    m_otherL.clear();
    m_otherR.clear();
}

bool StemTrackBuffer::isFrameReady(uint32_t frame) const {
    return frame < m_framesReady.load(std::memory_order_acquire);
}

bool StemTrackBuffer::isRangeReady(uint32_t startFrame, uint32_t numFrames) const {
    const uint32_t ready = m_framesReady.load(std::memory_order_acquire);
    return (startFrame + numFrames) <= ready;
}

void StemTrackBuffer::writeChunk(
        uint32_t startFrame,
        uint32_t numFrames,
        const float* voc_l, const float* voc_r,
        const float* drum_l, const float* drum_r,
        const float* bass_l, const float* bass_r,
        const float* other_l, const float* other_r) {
    if (startFrame >= m_totalFrames || numFrames == 0) {
        return;
    }

    const uint32_t count = std::min(numFrames, m_totalFrames - startFrame);

    if (voc_l) std::memcpy(&m_vocL[startFrame], voc_l, count * sizeof(float));
    if (voc_r) std::memcpy(&m_vocR[startFrame], voc_r, count * sizeof(float));
    if (drum_l) std::memcpy(&m_drumL[startFrame], drum_l, count * sizeof(float));
    if (drum_r) std::memcpy(&m_drumR[startFrame], drum_r, count * sizeof(float));
    if (bass_l) std::memcpy(&m_bassL[startFrame], bass_l, count * sizeof(float));
    if (bass_r) std::memcpy(&m_bassR[startFrame], bass_r, count * sizeof(float));
    if (other_l) std::memcpy(&m_otherL[startFrame], other_l, count * sizeof(float));
    if (other_r) std::memcpy(&m_otherR[startFrame], other_r, count * sizeof(float));

    const uint32_t endFrame = startFrame + count;
    uint32_t currentReady = m_framesReady.load(std::memory_order_relaxed);
    while (endFrame > currentReady) {
        if (m_framesReady.compare_exchange_weak(
                    currentReady, endFrame,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
            break;
        }
    }
}

void StemTrackBuffer::readFrames(
        uint32_t startFrame,
        uint32_t numFrames,
        float* voc_l, float* voc_r,
        float* drum_l, float* drum_r,
        float* bass_l, float* bass_r,
        float* other_l, float* other_r) const {
    if (numFrames == 0) {
        return;
    }

    if (startFrame >= m_totalFrames) {
        if (voc_l) std::memset(voc_l, 0, numFrames * sizeof(float));
        if (voc_r) std::memset(voc_r, 0, numFrames * sizeof(float));
        if (drum_l) std::memset(drum_l, 0, numFrames * sizeof(float));
        if (drum_r) std::memset(drum_r, 0, numFrames * sizeof(float));
        if (bass_l) std::memset(bass_l, 0, numFrames * sizeof(float));
        if (bass_r) std::memset(bass_r, 0, numFrames * sizeof(float));
        if (other_l) std::memset(other_l, 0, numFrames * sizeof(float));
        if (other_r) std::memset(other_r, 0, numFrames * sizeof(float));
        return;
    }

    const uint32_t count = std::min(numFrames, m_totalFrames - startFrame);

    if (voc_l) std::memcpy(voc_l, &m_vocL[startFrame], count * sizeof(float));
    if (voc_r) std::memcpy(voc_r, &m_vocR[startFrame], count * sizeof(float));
    if (drum_l) std::memcpy(drum_l, &m_drumL[startFrame], count * sizeof(float));
    if (drum_r) std::memcpy(drum_r, &m_drumR[startFrame], count * sizeof(float));
    if (bass_l) std::memcpy(bass_l, &m_bassL[startFrame], count * sizeof(float));
    if (bass_r) std::memcpy(bass_r, &m_bassR[startFrame], count * sizeof(float));
    if (other_l) std::memcpy(other_l, &m_otherL[startFrame], count * sizeof(float));
    if (other_r) std::memcpy(other_r, &m_otherR[startFrame], count * sizeof(float));

    if (count < numFrames) {
        const uint32_t rem = numFrames - count;
        if (voc_l) std::memset(voc_l + count, 0, rem * sizeof(float));
        if (voc_r) std::memset(voc_r + count, 0, rem * sizeof(float));
        if (drum_l) std::memset(drum_l + count, 0, rem * sizeof(float));
        if (drum_r) std::memset(drum_r + count, 0, rem * sizeof(float));
        if (bass_l) std::memset(bass_l + count, 0, rem * sizeof(float));
        if (bass_r) std::memset(bass_r + count, 0, rem * sizeof(float));
        if (other_l) std::memset(other_l + count, 0, rem * sizeof(float));
        if (other_r) std::memset(other_r + count, 0, rem * sizeof(float));
    }
}

} // namespace mixxx
