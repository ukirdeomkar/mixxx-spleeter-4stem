#include "engine/stems/stemworkerthread.h"
#include "engine/stems/stemseparator.h"
#include "engine/stems/stemcache.h"
#include "engine/stems/stemdecode.h"

#include <QDebug>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

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

void StemWorkerThread::loadTrack(TrackPointer pTrack, const QString& hqStemsPath) {
    QMutexLocker locker(&m_mutex);
    m_pNextTrack = std::move(pTrack);
    m_nextHqStemsPath = hqStemsPath;
    m_abortCurrentTrack.storeRelaxed(1);
    m_hasWork.storeRelaxed(1);
    m_condition.wakeOne();

    if (!isRunning()) {
        m_stopRequested.storeRelaxed(0);
        // Normal priority: separation has to stay ahead of playback. Low
        // priority loses too much CPU to the GUI thread on 2-core machines.
        start(QThread::NormalPriority);
    }
}

void StemWorkerThread::stop() {
    m_stopRequested.storeRelaxed(1);
    m_abortCurrentTrack.storeRelaxed(1);
    m_hasWork.storeRelaxed(0);
    QMutexLocker locker(&m_mutex);
    m_pNextTrack.reset();
    m_nextHqStemsPath.clear();
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
            m_currentHqStemsPath = m_nextHqStemsPath;
            m_nextHqStemsPath.clear();
            m_abortCurrentTrack.storeRelaxed(0);
        }

        if (!pTrack) {
            continue;
        }

        m_pCurrentTrack = pTrack;

        // 0. High quality (HT-Demucs) stems, if the user generated them for
        // this track. These are preferred over everything else.
        if (!m_currentHqStemsPath.isEmpty()) {
            if (StemCache::loadFromPath(m_currentHqStemsPath, m_pBuffer.get())) {
                qDebug() << "StemWorkerThread: high quality stems loaded for"
                         << pTrack->getTitle();
                if (m_pProgressControl) {
                    m_pProgressControl->set(1.0);
                }
                if (m_pStatusControl) {
                    m_pStatusControl->set(2.0); // 2 = AI Stems Ready!
                }
                m_currentHqStemsPath.clear();
                continue;
            }
            qDebug() << "StemWorkerThread: high quality stems unreadable,"
                     << "falling back to the fast engine:"
                     << m_currentHqStemsPath;
            m_currentHqStemsPath.clear();
        }

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
        std::atomic<bool> cancelRequested{false};

        const bool decoded = decodeTrackToStereoFloats(pTrack, &in_l, &in_r, &cancelRequested);
        const uint32_t totalFrames = decoded ? static_cast<uint32_t>(in_l.size()) : 0;
        if (totalFrames == 0) {
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
