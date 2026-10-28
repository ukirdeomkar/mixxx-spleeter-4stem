#include "engine/stems/stemseparator.h"

#ifdef MIXXX_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#ifdef __APPLE__
#include <coreml_provider_factory.h>
#elif defined(__ANDROID__) || defined(ANDROID)
#include <nnapi_provider_factory.h>
#endif
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace mixxx {

#ifdef MIXXX_HAS_ONNXRUNTIME

namespace {

constexpr char kInputName[] = "x";
constexpr char kOutputName[] = "y";

const char* const kStemFileNames[SpleeterEngine::kStemCount] = {
        "spleeter_vocals.onnx",
        "spleeter_drums.onnx",
        "spleeter_bass.onnx",
        "spleeter_other.onnx",
};

const char* const kStemLabels[SpleeterEngine::kStemCount] = {
        "vocals",
        "drums",
        "bass",
        "other",
};

// Spleeter's soft ratio mask epsilon: 1e-10 shared across all four stems.
constexpr float kMaskEpsilon = 1e-10f / SpleeterEngine::kStemCount;

QString findModelFile(const QString& filename) {
#if defined(__ANDROID__) || defined(ANDROID)
    // On Android the model may ship inside the APK; unpack it once.
    const QString assetPath = "assets:/models/" + filename;
    if (QFile::exists(assetPath)) {
        const QString destDir =
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
        QDir().mkpath(destDir);
        const QString destPath = destDir + "/" + filename;
        if (!QFileInfo::exists(destPath)) {
            QFile::copy(assetPath, destPath);
        }
        if (QFileInfo::exists(destPath)) {
            return destPath;
        }
    }
#endif

    const QStringList searchDirs = {
            QCoreApplication::applicationDirPath() + "/models",
            QCoreApplication::applicationDirPath() + "/../Resources/models",
            QDir::currentPath() + "/models",
            QDir::currentPath(),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models",
            QDir::homePath() + "/.mixxx/models",
            QDir::homePath() + "/Library/Application Support/Mixxx/models",
            "/usr/share/mixxx/models",
            "/usr/local/share/mixxx/models",
    };

    for (const auto& dir : searchDirs) {
        const QString candidate = dir + "/" + filename;
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

void configureSessionOptions(Ort::SessionOptions& options) {
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Four stems are inferred one after another on the worker thread. Four
    // intra-op threads measured fastest on the low-power Intel CPUs this fork
    // targets; more threads only adds scheduling overhead.
    const unsigned int hwThreads = std::thread::hardware_concurrency();
    const unsigned int numThreads =
            std::max(1u, std::min(4u, hwThreads == 0 ? 2u : hwThreads));
    options.SetIntraOpNumThreads(static_cast<int>(numThreads));
    options.SetInterOpNumThreads(1);
}

} // namespace

SpleeterEngine& SpleeterEngine::instance() {
    static SpleeterEngine* s_instance = new SpleeterEngine();
    return *s_instance;
}

SpleeterEngine::SpleeterEngine() = default;

SpleeterEngine::~SpleeterEngine() {
    finalize();
}

bool SpleeterEngine::locateModels(QString modelPaths[kStemCount]) const {
    for (int i = 0; i < kStemCount; ++i) {
        modelPaths[i] = findModelFile(kStemFileNames[i]);
        if (modelPaths[i].isEmpty()) {
            return false;
        }
    }
    return true;
}

bool SpleeterEngine::isAvailable() {
    QString paths[kStemCount];
    return locateModels(paths);
}

bool SpleeterEngine::isInitialized() const {
    if (!m_bInitialized || !m_pStft) {
        return false;
    }
    for (int i = 0; i < kStemCount; ++i) {
        if (!m_pSessions[i]) {
            return false;
        }
    }
    return true;
}

bool SpleeterEngine::initialize(const QString& customModelDir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (isInitialized()) {
        return true;
    }

    QString paths[kStemCount];
    if (!customModelDir.isEmpty()) {
        for (int i = 0; i < kStemCount; ++i) {
            const QString candidate = customModelDir + "/" + kStemFileNames[i];
            if (!QFileInfo::exists(candidate)) {
                qDebug() << "SpleeterEngine: missing model in custom dir:" << candidate;
                return false;
            }
            paths[i] = candidate;
        }
    } else if (!locateModels(paths)) {
        qDebug() << "SpleeterEngine: could not find all spleeter_*.onnx models;"
                 << "expected them next to the app, in ~/.mixxx/models or"
                 << "~/Library/Application Support/Mixxx/models";
        return false;
    }

    try {
        m_pEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SpleeterEngine");

        Ort::SessionOptions sessionOptions;
        configureSessionOptions(sessionOptions);

        for (int i = 0; i < kStemCount; ++i) {
            qDebug() << "SpleeterEngine: loading" << kStemLabels[i] << "from" << paths[i];
            m_pSessions[i] = std::make_unique<Ort::Session>(
                    *m_pEnv, paths[i].toUtf8().constData(), sessionOptions);
        }

        m_pMemoryInfo = std::make_unique<Ort::MemoryInfo>(
                Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        m_pStft = std::make_unique<spleeter::StftProcessor>();

        const size_t frames = spleeter::kFramesPerSplit;
        const size_t bins = spleeter::kBins;
        const size_t modelBins = spleeter::kModelBins;
        const size_t planeFloats = frames * modelBins;

        m_specL.assign(frames * bins * 2, 0.0f);
        m_specR.assign(frames * bins * 2, 0.0f);
        m_netInput.assign(2 * planeFloats, 0.0f);
        for (int i = 0; i < kStemCount; ++i) {
            m_estimates[i].assign(2 * planeFloats, 0.0f);
        }
        m_denominator.assign(2 * planeFloats, 0.0f);
        m_mask.assign(frames * modelBins, 0.0f);
        m_maskedSpec.assign(frames * bins * 2, 0.0f);
        m_ola.assign(frames * spleeter::kHopSize, 0.0f);

        m_bInitialized = true;
        qDebug() << "SpleeterEngine: 4-stem Spleeter models loaded";
        return true;
    } catch (const Ort::Exception& e) {
        qDebug() << "SpleeterEngine: ONNX initialization failed:" << e.what();
        finalize();
        return false;
    } catch (const std::exception& e) {
        qDebug() << "SpleeterEngine: initialization failed:" << e.what();
        finalize();
        return false;
    }
}

int SpleeterEngine::blockCount(uint32_t totalFrames) {
    return spleeter::blockCount(static_cast<int>(totalFrames));
}

int SpleeterEngine::blockSampleCount(uint32_t totalFrames, int blockIndex) {
    const int64_t start = static_cast<int64_t>(blockIndex) * spleeter::kSamplesPerBlock;
    if (start >= static_cast<int64_t>(totalFrames)) {
        return 0;
    }
    return static_cast<int>(std::min<int64_t>(
            spleeter::kSamplesPerBlock, static_cast<int64_t>(totalFrames) - start));
}

bool SpleeterEngine::separateBlock(const float* in_l,
        const float* in_r,
        uint32_t totalFrames,
        int blockIndex,
        float* voc_l, float* voc_r,
        float* drum_l, float* drum_r,
        float* bass_l, float* bass_r,
        float* other_l, float* other_r,
        std::atomic<bool>* pCancelFlag) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!in_l || !in_r || totalFrames == 0) {
        return false;
    }
    if (!m_bInitialized && !initialize()) {
        return false;
    }
    if (blockIndex < 0 || blockIndex >= blockCount(totalFrames)) {
        return false;
    }
    if (pCancelFlag && pCancelFlag->load(std::memory_order_relaxed)) {
        return false;
    }

    const int frames = spleeter::kFramesPerSplit;
    const int bins = spleeter::kBins;
    const int modelBins = spleeter::kModelBins;
    const int firstFrame = spleeter::blockFirstFrame(blockIndex);
    const size_t planeFloats = static_cast<size_t>(frames) * modelBins;

    try {
        // 1. STFT of this block's frames for both channels.
        m_pStft->forward(in_l, static_cast<int>(totalFrames), firstFrame, frames, m_specL.data());
        m_pStft->forward(in_r, static_cast<int>(totalFrames), firstFrame, frames, m_specR.data());

        // 2. Magnitudes of the modelled band -> network input [2, 1, 512, 1024].
        const float* planes[2] = {m_specL.data(), m_specR.data()};
        for (int ch = 0; ch < 2; ++ch) {
            const float* spec = planes[ch];
            float* dstPlane = m_netInput.data() + ch * planeFloats;
            for (int f = 0; f < frames; ++f) {
                const float* src = spec + static_cast<size_t>(f) * bins * 2;
                float* dst = dstPlane + static_cast<size_t>(f) * modelBins;
                for (int k = 0; k < modelBins; ++k) {
                    const float re = src[2 * k];
                    const float im = src[2 * k + 1];
                    dst[k] = std::sqrt(re * re + im * im);
                }
            }
        }

        const char* const inputNames[] = {kInputName};
        const char* const outputNames[] = {kOutputName};
        std::vector<int64_t> inputShape = {2, 1, frames, modelBins};
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(*m_pMemoryInfo,
                m_netInput.data(),
                m_netInput.size(),
                inputShape.data(),
                inputShape.size());

        // 3. Run the four stem models. The mask needs all four estimates.
        for (int s = 0; s < kStemCount; ++s) {
            if (pCancelFlag && pCancelFlag->load(std::memory_order_relaxed)) {
                return false;
            }
            auto outputs = m_pSessions[s]->Run(Ort::RunOptions{nullptr},
                    inputNames,
                    &inputTensor,
                    1,
                    outputNames,
                    1);
            if (outputs.empty()) {
                qDebug() << "SpleeterEngine: empty output for stem" << kStemLabels[s];
                return false;
            }
            const float* data = outputs[0].GetTensorData<float>();
            std::memcpy(m_estimates[s].data(), data, sizeof(float) * 2 * planeFloats);
        }

        // 4. Wiener-style denominator over all four stems.
        for (size_t i = 0; i < m_denominator.size(); ++i) {
            float sum = 1e-10f;
            for (int s = 0; s < kStemCount; ++s) {
                const float e = m_estimates[s][i];
                sum += e * e;
            }
            m_denominator[i] = sum;
        }

        float* outL[kStemCount] = {voc_l, drum_l, bass_l, other_l};
        float* outR[kStemCount] = {voc_r, drum_r, bass_r, other_r};
        const int sampleCount = blockSampleCount(totalFrames, blockIndex);

        // 5. Apply each stem's mask to the original complex spectrum, extend
        //    the band with the per-frame mask mean, then iSTFT.
        for (int s = 0; s < kStemCount; ++s) {
            if (!outL[s] || !outR[s]) {
                continue;
            }
            for (int ch = 0; ch < 2; ++ch) {
                if (pCancelFlag && pCancelFlag->load(std::memory_order_relaxed)) {
                    return false;
                }

                const float* est = m_estimates[s].data() + ch * planeFloats;
                const float* den = m_denominator.data() + ch * planeFloats;
                const float* spec = planes[ch];

                for (int f = 0; f < frames; ++f) {
                    const float* e = est + static_cast<size_t>(f) * modelBins;
                    const float* d = den + static_cast<size_t>(f) * modelBins;
                    float* maskRow = m_mask.data() + static_cast<size_t>(f) * modelBins;

                    float maskSum = 0.0f;
                    for (int k = 0; k < modelBins; ++k) {
                        const float value = e[k];
                        const float m = (value * value + kMaskEpsilon) / d[k];
                        maskRow[k] = m;
                        maskSum += m;
                    }
                    // Spleeter's `average` band extension: everything above the
                    // modelled band gets the per-frame mean of the mask.
                    const float tail = maskSum / static_cast<float>(modelBins);

                    float* dst = m_maskedSpec.data() + static_cast<size_t>(f) * bins * 2;
                    const float* src = spec + static_cast<size_t>(f) * bins * 2;
                    for (int k = 0; k < bins; ++k) {
                        const float m = (k < modelBins) ? maskRow[k] : tail;
                        dst[2 * k] = src[2 * k] * m;
                        dst[2 * k + 1] = src[2 * k + 1] * m;
                    }
                }

                m_pStft->inverse(m_maskedSpec.data(), frames, m_ola.data());

                // Only [kPad, kPad + sampleCount) is fully covered by this
                // block's frames; it maps to signal samples [start, start+count).
                float* dest = (ch == 0) ? outL[s] : outR[s];
                std::memcpy(dest,
                        m_ola.data() + spleeter::kPad,
                        sizeof(float) * sampleCount);
            }
        }

        // Optional self-check: four ratio masks sum to one, so the stems must
        // reconstruct the mix. Enable with MIXXX_DEBUG_SPLEETER_STEMS=1.
        if (qEnvironmentVariableIsSet("MIXXX_DEBUG_SPLEETER_STEMS") &&
                outL[0] && outR[0] && outL[1] && outR[1] &&
                outL[2] && outR[2] && outL[3] && outR[3]) {
            const uint32_t start = static_cast<uint32_t>(blockIndex) * kSamplesPerBlock;
            double errEnergy = 0.0;
            double refEnergy = 0.0;
            for (int i = 0; i < sampleCount; ++i) {
                const double sumL = static_cast<double>(outL[0][i]) + outL[1][i] +
                        outL[2][i] + outL[3][i];
                const double sumR = static_cast<double>(outR[0][i]) + outR[1][i] +
                        outR[2][i] + outR[3][i];
                const double refL = in_l[start + i];
                const double refR = in_r[start + i];
                errEnergy += (sumL - refL) * (sumL - refL) + (sumR - refR) * (sumR - refR);
                refEnergy += refL * refL + refR * refR;
            }
            const double residualDb = 10.0 * std::log10(
                    (errEnergy + 1e-30) / (refEnergy + 1e-30));
            qDebug() << "SpleeterEngine: block" << blockIndex
                     << "sum(stems) - mix =" << residualDb << "dB";
        }

        return true;
    } catch (const Ort::Exception& e) {
        qDebug() << "SpleeterEngine: inference failed:" << e.what();
        return false;
    } catch (const std::exception& e) {
        qDebug() << "SpleeterEngine: separation failed:" << e.what();
        return false;
    }
}

bool SpleeterEngine::separateTrack(uint32_t totalFrames,
        const float* in_l,
        const float* in_r,
        float* voc_l, float* voc_r,
        float* drum_l, float* drum_r,
        float* bass_l, float* bass_r,
        float* other_l, float* other_r,
        std::function<void(double)> progressCallback,
        std::atomic<bool>* pCancelFlag) {
    if (totalFrames == 0 || !in_l || !in_r) {
        return false;
    }

    const int blocks = blockCount(totalFrames);
    if (blocks <= 0) {
        return false;
    }

    std::vector<float> blockVocL(spleeter::kSamplesPerBlock);
    std::vector<float> blockVocR(spleeter::kSamplesPerBlock);
    std::vector<float> blockDrumL(spleeter::kSamplesPerBlock);
    std::vector<float> blockDrumR(spleeter::kSamplesPerBlock);
    std::vector<float> blockBassL(spleeter::kSamplesPerBlock);
    std::vector<float> blockBassR(spleeter::kSamplesPerBlock);
    std::vector<float> blockOtherL(spleeter::kSamplesPerBlock);
    std::vector<float> blockOtherR(spleeter::kSamplesPerBlock);

    for (int b = 0; b < blocks; ++b) {
        if (pCancelFlag && pCancelFlag->load(std::memory_order_relaxed)) {
            return false;
        }

        if (!separateBlock(in_l, in_r, totalFrames, b,
                    blockVocL.data(), blockVocR.data(),
                    blockDrumL.data(), blockDrumR.data(),
                    blockBassL.data(), blockBassR.data(),
                    blockOtherL.data(), blockOtherR.data(),
                    pCancelFlag)) {
            return false;
        }

        const uint32_t start = static_cast<uint32_t>(b) * spleeter::kSamplesPerBlock;
        const size_t count = static_cast<size_t>(blockSampleCount(totalFrames, b));
        if (count == 0) {
            break;
        }

        std::memcpy(voc_l + start, blockVocL.data(), sizeof(float) * count);
        std::memcpy(voc_r + start, blockVocR.data(), sizeof(float) * count);
        std::memcpy(drum_l + start, blockDrumL.data(), sizeof(float) * count);
        std::memcpy(drum_r + start, blockDrumR.data(), sizeof(float) * count);
        std::memcpy(bass_l + start, blockBassL.data(), sizeof(float) * count);
        std::memcpy(bass_r + start, blockBassR.data(), sizeof(float) * count);
        std::memcpy(other_l + start, blockOtherL.data(), sizeof(float) * count);
        std::memcpy(other_r + start, blockOtherR.data(), sizeof(float) * count);

        if (progressCallback) {
            progressCallback(static_cast<double>(b + 1) / blocks);
        }
    }

    return true;
}

void SpleeterEngine::finalize() {
    for (int i = 0; i < kStemCount; ++i) {
        m_pSessions[i].reset();
    }
    m_pMemoryInfo.reset();
    m_pEnv.reset();
    m_pStft.reset();
    m_bInitialized = false;
}

#else // !MIXXX_HAS_ONNXRUNTIME

SpleeterEngine& SpleeterEngine::instance() {
    static SpleeterEngine* s_instance = new SpleeterEngine();
    return *s_instance;
}

SpleeterEngine::SpleeterEngine() = default;
SpleeterEngine::~SpleeterEngine() = default;

bool SpleeterEngine::locateModels(QString* modelPaths) const {
    Q_UNUSED(modelPaths);
    return false;
}
bool SpleeterEngine::isAvailable() {
    return false;
}
bool SpleeterEngine::isInitialized() const {
    return false;
}
bool SpleeterEngine::initialize(const QString&) {
    return false;
}
int SpleeterEngine::blockCount(uint32_t) {
    return 0;
}
int SpleeterEngine::blockSampleCount(uint32_t, int) {
    return 0;
}
bool SpleeterEngine::separateBlock(const float*, const float*, uint32_t, int,
        float*, float*, float*, float*, float*, float*, float*, float*,
        std::atomic<bool>*) {
    return false;
}
bool SpleeterEngine::separateTrack(uint32_t, const float*, const float*,
        float*, float*, float*, float*, float*, float*, float*, float*,
        std::function<void(double)>, std::atomic<bool>*) {
    return false;
}
void SpleeterEngine::finalize() {
}

#endif // MIXXX_HAS_ONNXRUNTIME

} // namespace mixxx
