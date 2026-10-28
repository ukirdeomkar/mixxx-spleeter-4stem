#include "engine/stems/stemworkerthread.h"
#include "engine/stems/stemseparator.h"
#include "engine/stems/stemcache.h"
#include "sources/soundsourceproxy.h"
#include "sources/audiosource.h"
#include "util/indexrange.h"
#include "util/samplebuffer.h"

#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QDateTime>
#include <vector>
#include <algorithm>

namespace mixxx {

StemWorkerThread::StemWorkerThread(
        std::shared_ptr<StemTrackBuffer> pBuffer,
        ControlObject* pProgressControl,
        ControlObject* pStatusControl)
        : m_pBuffer(std::move(pBuffer)),
          m_pProgressControl(pProgressControl),
          m_pStatusControl(pStatusControl) {
}

StemWorkerThread::~StemWorkerThread() {
    stop();
    wait();
}

void StemWorkerThread::loadTrack(TrackPointer pTrack) {
    QMutexLocker locker(&m_mutex);
    m_pNextTrack = std::move(pTrack);
    m_abortCurrentTrack.storeRelaxed(1);
    m_hasWork.storeRelaxed(1);
    m_condition.wakeOne();

    if (!isRunning()) {
        m_stopRequested.storeRelaxed(0);
        start(QThread::LowPriority);
    }
}

void StemWorkerThread::stop() {
    m_stopRequested.storeRelaxed(1);
    m_abortCurrentTrack.storeRelaxed(1);
    m_hasWork.storeRelaxed(0);
    QMutexLocker locker(&m_mutex);
    m_pNextTrack.reset();
    m_condition.wakeOne();
}

void StemWorkerThread::run() {
    while (!m_stopRequested.loadAcquire()) {
        TrackPointer pTrack;
        {
            QMutexLocker locker(&m_mutex);
            while (!m_pNextTrack && !m_stopRequested.loadAcquire()) {
                m_condition.wait(&m_mutex);
            }
            if (m_stopRequested.loadAcquire()) {
                break;
            }
            pTrack = m_pNextTrack;
            m_pNextTrack.reset();
            m_abortCurrentTrack.storeRelaxed(0);
        }

        if (!pTrack) {
            continue;
        }

        m_pCurrentTrack = pTrack;

        // 1. Check if stems are already cached on disk (VirtualDJ-style instant load)
        if (StemCache::hasCachedStems(pTrack)) {
            if (StemCache::loadFromCache(pTrack, m_pBuffer.get())) {
                qDebug() << "StemWorkerThread: Stems instantly loaded from cache for"
                         << pTrack->getTitle();
                if (m_pProgressControl) {
                    m_pProgressControl->set(1.0);
                }
                if (m_pStatusControl) {
                    m_pStatusControl->set(2.0); // 2 = AI Stems Ready!
                }
                continue;
            }
        }

        if (m_pStatusControl) {
            m_pStatusControl->set(1.0); // 1 = Analyzing
        }
        if (m_pProgressControl) {
            m_pProgressControl->set(0.0);
        }

        // Check if the Spleeter ONNX models are available
        if (!SpleeterEngine::instance().isAvailable()) {
            qDebug() << "StemWorkerThread: Spleeter ONNX models not available, aborting ML separation";
            if (m_pStatusControl) {
                m_pStatusControl->set(0.0); // 0 = Idle
            }
            continue;
        }

        std::vector<float> in_l;
        std::vector<float> in_r;
        uint32_t totalFrames = 0;

        // Attempt 1: Try decoding via Mixxx SoundSourceProxy
        mixxx::AudioSource::OpenParams config;
        config.setChannelCount(mixxx::audio::ChannelCount::stereo());
        auto pAudioSource = SoundSourceProxy(pTrack).openAudioSource(config);

        if (pAudioSource && pAudioSource->verifyReadable() && pAudioSource->frameLength() > 0) {
            totalFrames = static_cast<uint32_t>(pAudioSource->frameLength());
            in_l.resize(totalFrames);
            in_r.resize(totalFrames);

            constexpr SINT kBlockSize = 65536;
            mixxx::SampleBuffer sampleBuffer(kBlockSize * 2);
            SINT framesReadTotal = 0;
            while (framesReadTotal < static_cast<SINT>(totalFrames)) {
                if (m_stopRequested.loadAcquire() || m_abortCurrentTrack.loadAcquire()) {
                    break;
                }
                const SINT framesToRead = std::min(kBlockSize, static_cast<SINT>(totalFrames) - framesReadTotal);
                const auto chunkRange = mixxx::IndexRange::between(framesReadTotal, framesReadTotal + framesToRead);
                const auto readableFrames = pAudioSource->readSampleFrames(
                        mixxx::WritableSampleFrames(chunkRange, mixxx::SampleBuffer::WritableSlice(sampleBuffer)));
                const SINT count = readableFrames.frameLength();
                if (count <= 0) {
                    break;
                }
                for (SINT i = 0; i < count; ++i) {
                    in_l[framesReadTotal + i] = static_cast<float>(sampleBuffer[2 * i]);
                    in_r[framesReadTotal + i] = static_cast<float>(sampleBuffer[2 * i + 1]);
                }
                framesReadTotal += count;
            }
        }

        // Attempt 2: Fallback to native platform audio converter (afconvert on macOS)
        if (in_l.empty() || totalFrames == 0) {
#ifdef __APPLE__
            const QString inputPath = pTrack->getLocation();
            const QString tempWavPath = QString("/tmp/mixxx_stem_decode_%1.wav")
                    .arg(QDateTime::currentMSecsSinceEpoch());

            QStringList args;
            args << "-f" << "WAVE" << "-d" << "LEF32@44100" << "-c" << "2" << inputPath << tempWavPath;
            int ret = QProcess::execute("afconvert", args);
            if (ret == 0) {
                QFile wavFile(tempWavPath);
                if (wavFile.open(QIODevice::ReadOnly)) {
                    QByteArray header = wavFile.read(8192);
                    int dataPos = header.indexOf("data");
                    if (dataPos >= 0) {
                        wavFile.seek(dataPos + 4);
                        uint32_t dataBytes = 0;
                        wavFile.read(reinterpret_cast<char*>(&dataBytes), sizeof(uint32_t));
                        totalFrames = dataBytes / (2 * sizeof(float));

                        wavFile.seek(dataPos + 8);
                        std::vector<float> interleaved(totalFrames * 2);
                        qint64 readBytes = wavFile.read(reinterpret_cast<char*>(interleaved.data()), dataBytes);
                        uint32_t actualFrames = static_cast<uint32_t>(readBytes / (2 * sizeof(float)));
                        totalFrames = actualFrames;

                        in_l.resize(totalFrames);
                        in_r.resize(totalFrames);
                        for (uint32_t i = 0; i < totalFrames; ++i) {
                            in_l[i] = interleaved[2 * i];
                            in_r[i] = interleaved[2 * i + 1];
                        }
                    }
                    wavFile.close();
                }
                QFile::remove(tempWavPath);
            }
#endif
        }

        if (totalFrames == 0 || in_l.empty()) {
            qDebug() << "StemWorkerThread: Failed to decode audio for" << pTrack->getLocation();
            if (m_pStatusControl) {
                m_pStatusControl->set(0.0);
            }
            continue;
        }

        m_pBuffer->allocate(totalFrames);

        const int blocks = SpleeterEngine::blockCount(totalFrames);
        qDebug() << "StemWorkerThread: progressive Spleeter 4-stem separation of"
                 << pTrack->getTitle()
                 << "(" << totalFrames << "frames," << blocks << "blocks)";

        // Block scratch: one model block is 512 STFT frames and yields
        // 521216 samples per stem/channel.
        const size_t blockSamples = static_cast<size_t>(SpleeterEngine::kSamplesPerBlock);
        std::vector<float> vocL(blockSamples);
        std::vector<float> vocR(blockSamples);
        std::vector<float> drumL(blockSamples);
        std::vector<float> drumR(blockSamples);
        std::vector<float> bassL(blockSamples);
        std::vector<float> bassR(blockSamples);
        std::vector<float> otherL(blockSamples);
        std::vector<float> otherR(blockSamples);

        bool ok = true;
        for (int b = 0; b < blocks; ++b) {
            if (m_stopRequested.loadAcquire() || m_abortCurrentTrack.loadAcquire()) {
                ok = false;
                break;
            }

            if (!SpleeterEngine::instance().separateBlock(
                        in_l.data(), in_r.data(), totalFrames, b,
                        vocL.data(), vocR.data(),
                        drumL.data(), drumR.data(),
                        bassL.data(), bassR.data(),
                        otherL.data(), otherR.data(),
                        nullptr)) {
                ok = false;
                break;
            }

            const uint32_t startFrame =
                    static_cast<uint32_t>(b) * SpleeterEngine::kSamplesPerBlock;
            const uint32_t count = static_cast<uint32_t>(
                    SpleeterEngine::blockSampleCount(totalFrames, b));
            if (count == 0) {
                break;
            }

            // Publish this block immediately so the deck can switch from the
            // DSP fallback to the real stems as soon as the first one lands.
            m_pBuffer->writeChunk(startFrame, count,
                    vocL.data(), vocR.data(),
                    drumL.data(), drumR.data(),
                    bassL.data(), bassR.data(),
                    otherL.data(), otherR.data());

            if (m_pProgressControl) {
                m_pProgressControl->set(static_cast<double>(b + 1) / blocks);
            }
        }

        if (ok && m_pBuffer->framesReady() >= totalFrames) {
            // Raw float stems would be ~420 MB per five minute track, so disk
            // caching stays off until a compressed format lands (see docs).
            if (StemCache::isEnabled()) {
                StemCache::saveToCache(pTrack, m_pBuffer.get());
            }
            qDebug() << "StemWorkerThread: separated 4 stems for" << pTrack->getTitle();

            if (m_pProgressControl) {
                m_pProgressControl->set(1.0);
            }
            if (m_pStatusControl) {
                m_pStatusControl->set(2.0); // 2 = AI Stems Ready!
            }
        } else {
            qDebug() << "StemWorkerThread: Spleeter separation aborted or failed for"
                     << pTrack->getTitle()
                     << "- ready frames:" << m_pBuffer->framesReady() << "/" << totalFrames;
            if (m_pStatusControl) {
                m_pStatusControl->set(0.0);
            }
        }
    }
}

} // namespace mixxx

#include "moc_stemworkerthread.cpp"
