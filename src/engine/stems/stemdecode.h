#pragma once

#include <atomic>
#include <vector>

#include "track/track.h"

namespace mixxx {

/// Decode a track into planar stereo float vectors at the source sample rate.
///
/// Used by both the real-time Spleeter worker (which separates block by block)
/// and the offline high quality (HT-Demucs) job, which needs the whole track in
/// memory anyway. Safe to call from a non-GUI thread.
///
/// Returns false if the track could not be decoded, in which case both vectors
/// are left empty.
bool decodeTrackToStereoFloats(const TrackPointer& pTrack,
        std::vector<float>* pLeft,
        std::vector<float>* pRight,
        std::atomic<bool>* pCancelFlag = nullptr);

} // namespace mixxx
