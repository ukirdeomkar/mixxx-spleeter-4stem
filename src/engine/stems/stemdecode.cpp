#include "engine/stems/stemdecode.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStringList>

#include <algorithm>
#include <cstdint>

#include "sources/audiosource.h"
#include "sources/soundsourceproxy.h"
#include "util/indexrange.h"
#include "util/samplebuffer.h"

namespace mixxx {

namespace {

/// Attempt 1: decode through Mixxx's own SoundSourceProxy (works for every
/// format the library can play).
bool decodeWithSoundSource(const TrackPointer& pTrack,
        std::vector<float>* pLeft,
        std::vector<float>* pRight,
        std::atomic<bool>* pCancelFlag) {
    mixxx::AudioSource::OpenParams config;
    config.setChannelCount(mixxx::audio::ChannelCount::stereo());
    auto pAudioSource = SoundSourceProxy(pTrack).openAudioSource(config);
    if (!pAudioSource || !pAudioSource->verifyReadable() || pAudioSource->frameLength() <= 0) {
        return false;
    }

    const auto totalFrames = static_cast<uint32_t>(pAudioSource->frameLength());
    pLeft->resize(totalFrames);
    pRight->resize(totalFrames);

    constexpr SINT kBlockSize = 65536;
    mixxx::SampleBuffer sampleBuffer(kBlockSize * 2);
    SINT framesReadTotal = 0;
    while (framesReadTotal < static_cast<SINT>(totalFrames)) {
        if (pCancelFlag && pCancelFlag->load(std::memory_order_relaxed)) {
            break;
        }
        const SINT framesToRead =
                std::min(kBlockSize, static_cast<SINT>(totalFrames) - framesReadTotal);
        const auto chunkRange = mixxx::IndexRange::between(
                framesReadTotal, framesReadTotal + framesToRead);
        const auto readableFrames = pAudioSource->readSampleFrames(
                mixxx::WritableSampleFrames(chunkRange,
                        mixxx::SampleBuffer::WritableSlice(sampleBuffer)));
        const SINT count = readableFrames.frameLength();
        if (count <= 0) {
            break;
        }
        for (SINT i = 0; i < count; ++i) {
            (*pLeft)[framesReadTotal + i] = static_cast<float>(sampleBuffer[2 * i]);
            (*pRight)[framesReadTotal + i] = static_cast<float>(sampleBuffer[2 * i + 1]);
        }
        framesReadTotal += count;
    }

    if (framesReadTotal <= 0) {
        pLeft->clear();
        pRight->clear();
        return false;
    }
    pLeft->resize(static_cast<size_t>(framesReadTotal));
    pRight->resize(static_cast<size_t>(framesReadTotal));
    return true;
}

#ifdef __APPLE__
/// Attempt 2 (macOS only): native CoreAudio conversion, used when the
/// SoundSource plugins cannot open the file.
bool decodeWithAfconvert(const TrackPointer& pTrack,
        std::vector<float>* pLeft,
        std::vector<float>* pRight) {
    const QString tempWavPath = QDir::tempPath() + QStringLiteral("/mixxx_stem_decode_%1.wav")
            .arg(QDateTime::currentMSecsSinceEpoch());

    QStringList args;
    args << "-f" << "WAVE" << "-d" << "LEF32@44100" << "-c" << "2"
         << pTrack->getLocation() << tempWavPath;
    if (QProcess::execute("afconvert", args) != 0) {
        return false;
    }

    bool ok = false;
    QFile wavFile(tempWavPath);
    if (wavFile.open(QIODevice::ReadOnly)) {
        const QByteArray header = wavFile.read(8192);
        const int dataPos = header.indexOf("data");
        if (dataPos >= 0) {
            wavFile.seek(dataPos + 4);
            uint32_t dataBytes = 0;
            wavFile.read(reinterpret_cast<char*>(&dataBytes), sizeof(uint32_t));
            wavFile.seek(dataPos + 8);

            std::vector<float> interleaved(static_cast<size_t>(dataBytes / sizeof(float)));
            const qint64 readBytes = wavFile.read(
                    reinterpret_cast<char*>(interleaved.data()), dataBytes);
            const uint32_t totalFrames =
                    static_cast<uint32_t>(readBytes / (2 * sizeof(float)));
            if (totalFrames > 0) {
                pLeft->resize(totalFrames);
                pRight->resize(totalFrames);
                for (uint32_t i = 0; i < totalFrames; ++i) {
                    (*pLeft)[i] = interleaved[2 * i];
                    (*pRight)[i] = interleaved[2 * i + 1];
                }
                ok = true;
            }
        }
        wavFile.close();
    }
    QFile::remove(tempWavPath);
    return ok;
}
#endif

} // namespace

bool decodeTrackToStereoFloats(const TrackPointer& pTrack,
        std::vector<float>* pLeft,
        std::vector<float>* pRight,
        std::atomic<bool>* pCancelFlag) {
    if (!pTrack || !pLeft || !pRight) {
        return false;
    }
    pLeft->clear();
    pRight->clear();

    if (decodeWithSoundSource(pTrack, pLeft, pRight, pCancelFlag)) {
        return true;
    }

#ifdef __APPLE__
    if (decodeWithAfconvert(pTrack, pLeft, pRight)) {
        return true;
    }
#endif

    qDebug() << "Stem decode: failed to decode audio for" << pTrack->getLocation();
    pLeft->clear();
    pRight->clear();
    return false;
}

} // namespace mixxx
