#include "engine/stems/htdemucsengine.h"

#include "engine/stems/stemmodelpaths.h"

#ifdef MIXXX_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

#include <QDebug>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <thread>

namespace mixxx {

#ifdef MIXXX_HAS_ONNXRUNTIME

namespace {

constexpr char kModelFileName[] = "htdemucs.onnx";

// Model stem order of the StemSplitio HT-Demucs export:
// 0 = drums, 1 = bass, 2 = other, 3 = vocals.
int modelStemToBufferPlane(int modelStem) {
    switch (modelStem) {
    case 0: // drums
        return 2;
    case 1: // bass
        return 4;
    case 2: // other
        return 6;
    default: // vocals
        return 0;
    }
}

void configureSessionOptions(Ort::SessionOptions& options) {
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // Separation happens offline, so a few threads are free to use, but this
    // model is memory-bound; more than 4 threads measured slower on the
    // 2-core machines this fork targets.
    const unsigned int hwThreads = std::thread::hardware_concurrency();
    const unsigned int numThreads =
            std::max(1u, std::min(4u, hwThreads == 0 ? 2u : hwThreads));
    options.SetIntraOpNumThreads(static_cast<int>(numThreads));
    options.SetInterOpNumThreads(1);
}

} // namespace

HTDemucsEngine& HTDemucsEngine::instance() {
    static HTDemucsEngine* s_instance = new HTDemucsEngine();
    return *s_instance;
}

HTDemucsEngine::HTDemucsEngine() {
    // Trapezoidal overlap-add window: linear fade in / out over the 25 %
    // overlap, flat in the middle. The half-sample offset keeps the first and
    // last sample of a segment non-zero, so every frame is covered by a
    // strictly positive weight and the de-overlap normalisation never divides
    // by zero. A rising and a falling edge sum to exactly 1.
    m_window.assign(kSegmentSamples, 1.0f);
    for (int i = 0; i < kOverlapSamples; ++i) {
        const float fade =
                (static_cast<float>(i) + 0.5f) / static_cast<float>(kOverlapSamples);
        m_window[static_cast<size_t>(i)] = fade;
        m_window[static_cast<size_t>(kSegmentSamples - 1 - i)] = fade;
    }
}

HTDemucsEngine::~HTDemucsEngine() {
    finalize();
}

bool HTDemucsEngine::isAvailable() {
    return !findStemModelFile(kModelFileName).isEmpty();
}

bool HTDemucsEngine::isInitialized() const {
    return m_bInitialized && m_pSession && m_pMemoryInfo;
}

bool HTDemucsEngine::initialize(const QString& customModelPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (isInitialized()) {
        return true;
    }

    QString modelPath = customModelPath;
    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
        modelPath = findStemModelFile(kModelFileName);
    }
    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
        qDebug() << "HTDemucsEngine: no htdemucs.onnx found; high quality stems"
                 << "stay disabled (see docs/todo-hq-demucs.md for the download URL)";
        return false;
    }

    try {
        qDebug() << "HTDemucsEngine: loading" << modelPath;

        m_pEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "HTDemucsEngine");

        Ort::SessionOptions sessionOptions;
        configureSessionOptions(sessionOptions);

#ifdef _WIN32
        // Ort::Session takes a wchar_t path on Windows, UTF-8 elsewhere.
        const std::wstring widePath = modelPath.toStdWString();
        m_pSession = std::make_unique<Ort::Session>(
                *m_pEnv, widePath.c_str(), sessionOptions);
#else
        m_pSession = std::make_unique<Ort::Session>(
                *m_pEnv, modelPath.toUtf8().constData(), sessionOptions);
#endif

        Ort::AllocatorWithDefaultOptions allocator;
        m_inputName = m_pSession->GetInputNameAllocated(0, allocator).get();
        m_outputName = m_pSession->GetOutputNameAllocated(0, allocator).get();

        m_pMemoryInfo = std::make_unique<Ort::MemoryInfo>(
                Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        m_input.assign(2 * kSegmentSamples, 0.0f);
        for (int i = 0; i < kStemCount * 2; ++i) {
            m_chunk[i].assign(kSegmentSamples, 0.0f);
            m_acc[i].assign(kSegmentSamples, 0.0f);
        }
        m_weight.assign(kSegmentSamples, 0.0f);

        m_bInitialized = true;
        qDebug() << "HTDemucsEngine: HT-Demucs v4 model loaded"
                 << "(input" << QString::fromStdString(m_inputName)
                 << ", output" << QString::fromStdString(m_outputName) << ")";
        return true;
    } catch (const Ort::Exception& e) {
        qDebug() << "HTDemucsEngine: ONNX initialization failed:" << e.what();
        finalize();
        return false;
    } catch (const std::exception& e) {
        qDebug() << "HTDemucsEngine: initialization failed:" << e.what();
        finalize();
        return false;
    }
}

bool HTDemucsEngine::separateSegment() {
    // A model that was exported with a different segment length, element type or
    // I/O layout makes Run() throw. Contain that here: the caller runs on a
    // worker thread with no handler, and an uncaught Ort::Exception would take
    // the whole application down over a bad download.
    try {
        const std::vector<int64_t> inputShape = {1, 2, kSegmentSamples};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(*m_pMemoryInfo,
                m_input.data(),
                m_input.size(),
                inputShape.data(),
                inputShape.size());

        const char* const inputNames[] = {m_inputName.c_str()};
        const char* const outputNames[] = {m_outputName.c_str()};

        auto outputs = m_pSession->Run(Ort::RunOptions{nullptr},
                inputNames,
                &inputTensor,
                1,
                outputNames,
                1);
        if (outputs.empty()) {
            qDebug() << "HTDemucsEngine: empty output tensor";
            return false;
        }

        const auto outputInfo = outputs[0].GetTensorTypeAndShapeInfo();
        const size_t expectedFloats =
                static_cast<size_t>(kStemCount) * 2 * kSegmentSamples;
        if (outputInfo.GetElementCount() != expectedFloats) {
            qDebug() << "HTDemucsEngine: unexpected output element count"
                     << outputInfo.GetElementCount() << "expected" << expectedFloats;
            return false;
        }

        const float* outData = outputs[0].GetTensorData<float>();
        for (int modelStem = 0; modelStem < kStemCount; ++modelStem) {
            const int plane = modelStemToBufferPlane(modelStem);
            const float* src = outData +
                    static_cast<size_t>(modelStem) * 2 * kSegmentSamples;
            std::memcpy(m_chunk[plane].data(),
                    src,
                    sizeof(float) * kSegmentSamples);
            std::memcpy(m_chunk[plane + 1].data(),
                    src + kSegmentSamples,
                    sizeof(float) * kSegmentSamples);
        }
        return true;
    } catch (const Ort::Exception& e) {
        qDebug() << "HTDemucsEngine: segment inference failed:" << e.what();
        return false;
    } catch (const std::exception& e) {
        qDebug() << "HTDemucsEngine: segment inference failed:" << e.what();
        return false;
    }
}

bool HTDemucsEngine::separateTrack(uint32_t totalFrames,
        const float* in_l,
        const float* in_r,
        const ChunkCallback& chunkCallback,
        std::function<void(double)> progressCallback,
        std::atomic<bool>* pCancelFlag) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!in_l || !in_r || totalFrames == 0 || !chunkCallback) {
        return false;
    }
    if (!m_bInitialized && !initialize()) {
        return false;
    }

    // The overlap-add accumulators are shared by every run. A cancelled or
    // failed separation returns before the final segment drains them, so start
    // from a clean slate; otherwise the first overlap region of the next track
    // would be mixed with the previous track's tail.
    for (auto& plane : m_acc) {
        std::fill(plane.begin(), plane.end(), 0.0f);
    }
    std::fill(m_weight.begin(), m_weight.end(), 0.0f);

    const uint32_t segmentCount =
            (totalFrames + kStrideSamples - 1) / kStrideSamples;

    for (uint32_t segIndex = 0; segIndex < segmentCount; ++segIndex) {
        if (pCancelFlag && pCancelFlag->load(std::memory_order_relaxed)) {
            return false;
        }

        const uint32_t start = segIndex * kStrideSamples;
        const uint32_t copyCount =
                std::min<uint32_t>(kSegmentSamples, totalFrames - start);

        // The model wants a full 7.8 s segment; zero-pad the tail of the track.
        std::fill(m_input.begin(), m_input.end(), 0.0f);
        std::memcpy(m_input.data(), in_l + start, copyCount * sizeof(float));
        std::memcpy(m_input.data() + kSegmentSamples,
                in_r + start,
                copyCount * sizeof(float));

        if (!separateSegment()) {
            return false;
        }

        // Accumulate this segment with the overlap window.
        for (uint32_t i = 0; i < copyCount; ++i) {
            const float w = m_window[i];
            for (int p = 0; p < kStemCount * 2; ++p) {
                m_acc[p][i] += m_chunk[p][i] * w;
            }
            m_weight[i] += w;
        }

        // Everything before start + stride cannot be touched by the next
        // segment, so it is final and can be handed out.
        const uint32_t finalCount =
                std::min<uint32_t>(kStrideSamples, totalFrames - start);
        for (uint32_t i = 0; i < finalCount; ++i) {
            const float invWeight =
                    m_weight[i] > 1e-6f ? 1.0f / m_weight[i] : 0.0f;
            for (int p = 0; p < kStemCount * 2; ++p) {
                m_acc[p][i] *= invWeight;
            }
        }
        chunkCallback(start,
                finalCount,
                m_acc[0].data(),
                m_acc[1].data(),
                m_acc[2].data(),
                m_acc[3].data(),
                m_acc[4].data(),
                m_acc[5].data(),
                m_acc[6].data(),
                m_acc[7].data());

        // Keep the still-open overlap tail at the front of the accumulator;
        // it lines up with the start of the next segment.
        const uint32_t leftover =
                copyCount > kStrideSamples ? copyCount - kStrideSamples : 0;
        for (auto& plane : m_acc) {
            if (leftover > 0) {
                std::memmove(plane.data(),
                        plane.data() + kStrideSamples,
                        leftover * sizeof(float));
            }
            std::fill(plane.begin() + leftover, plane.end(), 0.0f);
        }
        if (leftover > 0) {
            std::memmove(m_weight.data(),
                    m_weight.data() + kStrideSamples,
                    leftover * sizeof(float));
        }
        std::fill(m_weight.begin() + leftover, m_weight.end(), 0.0f);

        if (progressCallback) {
            progressCallback(
                    static_cast<double>(segIndex + 1) / segmentCount);
        }
    }

    return true;
}

void HTDemucsEngine::finalize() {
    m_pSession.reset();
    m_pMemoryInfo.reset();
    m_pEnv.reset();
    m_inputName.clear();
    m_outputName.clear();
    m_bInitialized = false;
}

#else // !MIXXX_HAS_ONNXRUNTIME

HTDemucsEngine& HTDemucsEngine::instance() {
    static HTDemucsEngine* s_instance = new HTDemucsEngine();
    return *s_instance;
}

HTDemucsEngine::HTDemucsEngine() {}
HTDemucsEngine::~HTDemucsEngine() {}
bool HTDemucsEngine::isAvailable() {
    return false;
}
bool HTDemucsEngine::isInitialized() const {
    return false;
}
bool HTDemucsEngine::initialize(const QString&) {
    return false;
}
bool HTDemucsEngine::separateSegment() {
    return false;
}
bool HTDemucsEngine::separateTrack(uint32_t,
        const float*,
        const float*,
        const ChunkCallback&,
        std::function<void(double)>,
        std::atomic<bool>*) {
    return false;
}
void HTDemucsEngine::finalize() {}

#endif // MIXXX_HAS_ONNXRUNTIME

} // namespace mixxx
