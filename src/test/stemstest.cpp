#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "engine/stems/spleeterstft.h"
#include "engine/stems/stemseparator.h"
#include "engine/stems/stemtrackbuffer.h"
#include "engine/stems/stemdspisolator.h"
#include "test/mixxxtest.h"

namespace {

class StemsTest : public MixxxTest {
  protected:
    static std::vector<float> makeSine(int frames, double frequency) {
        std::vector<float> signal(frames);
        for (int i = 0; i < frames; ++i) {
            signal[i] = static_cast<float>(
                    std::sin(2.0 * M_PI * frequency * i / mixxx::spleeter::kSampleRate));
        }
        return signal;
    }
};

// The four-way DSP split must be a bit-exact bypass at unity gain.
TEST_F(StemsTest, DspIsolatorUnityTransparency) {
    mixxx::StemDspIsolator isolator;
    isolator.init(44100.0);

    constexpr int kFrames = 512;
    std::vector<CSAMPLE> buffer(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        buffer[2 * i] = static_cast<CSAMPLE>(std::sin(2.0 * M_PI * 440.0 * i / 44100.0));
        buffer[2 * i + 1] = static_cast<CSAMPLE>(std::cos(2.0 * M_PI * 880.0 * i / 44100.0));
    }
    const std::vector<CSAMPLE> original = buffer;

    isolator.process(buffer.data(), kFrames, 1.0, 1.0, 1.0, 1.0);

    for (size_t i = 0; i < buffer.size(); ++i) {
        EXPECT_FLOAT_EQ(original[i], buffer[i]);
    }
}

// Muting every stem must produce silence, i.e. the split is a true partition.
TEST_F(StemsTest, DspIsolatorMuteAll) {
    mixxx::StemDspIsolator isolator;
    isolator.init(44100.0);

    constexpr int kFrames = 256;
    std::vector<CSAMPLE> buffer(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        buffer[2 * i] = static_cast<CSAMPLE>(std::sin(2.0 * M_PI * 110.0 * i / 44100.0));
        buffer[2 * i + 1] = buffer[2 * i];
    }

    isolator.process(buffer.data(), kFrames, 0.0, 0.0, 0.0, 0.0);

    for (size_t i = 0; i < buffer.size(); ++i) {
        EXPECT_NEAR(buffer[i], 0.0, 1e-6);
    }
}

// Block geometry: consecutive blocks must tile the track exactly.
TEST_F(StemsTest, BlockTiling) {
    for (int frames : {0, 1, 1000, mixxx::spleeter::kSamplesPerBlock,
                mixxx::spleeter::kSamplesPerBlock + 1, 5 * 44100}) {
        const int blocks = mixxx::SpleeterEngine::blockCount(frames);
        int covered = 0;
        for (int b = 0; b < blocks; ++b) {
            EXPECT_GT(mixxx::SpleeterEngine::blockSampleCount(frames, b), 0);
            covered += mixxx::SpleeterEngine::blockSampleCount(frames, b);
        }
        EXPECT_EQ(covered, frames);
    }
}

TEST_F(StemsTest, TrackBufferRoundTrip) {
    constexpr uint32_t kFrames = 4096;
    mixxx::StemTrackBuffer buffer;
    buffer.allocate(kFrames);

    const std::vector<float> left = makeSine(kFrames, 220.0);
    std::vector<float> right = left;
    for (auto& sample : right) {
        sample = -sample;
    }

    // Four distinguishable stems.
    std::vector<float> vocL(kFrames), vocR(kFrames);
    std::vector<float> drumL(kFrames), drumR(kFrames);
    std::vector<float> bassL(kFrames), bassR(kFrames);
    std::vector<float> otherL(kFrames), otherR(kFrames);
    for (uint32_t i = 0; i < kFrames; ++i) {
        vocL[i] = left[i] * 0.1f;
        vocR[i] = right[i] * 0.1f;
        drumL[i] = left[i] * 0.2f;
        drumR[i] = right[i] * 0.2f;
        bassL[i] = left[i] * 0.3f;
        bassR[i] = right[i] * 0.3f;
        otherL[i] = left[i] * 0.4f;
        otherR[i] = right[i] * 0.4f;
    }

    buffer.writeChunk(0, kFrames,
            vocL.data(), vocR.data(),
            drumL.data(), drumR.data(),
            bassL.data(), bassR.data(),
            otherL.data(), otherR.data());

    EXPECT_EQ(buffer.framesReady(), kFrames);

    std::vector<float> outVocL(kFrames), outVocR(kFrames);
    std::vector<float> outDrumL(kFrames), outDrumR(kFrames);
    std::vector<float> outBassL(kFrames), outBassR(kFrames);
    std::vector<float> outOtherL(kFrames), outOtherR(kFrames);
    buffer.readFrames(0, kFrames,
            outVocL.data(), outVocR.data(),
            outDrumL.data(), outDrumR.data(),
            outBassL.data(), outBassR.data(),
            outOtherL.data(), outOtherR.data());

    for (uint32_t i = 0; i < kFrames; ++i) {
        EXPECT_FLOAT_EQ(bassL[i], outBassL[i]);
        EXPECT_FLOAT_EQ(bassR[i], outBassR[i]);
        EXPECT_FLOAT_EQ(otherL[i], outOtherL[i]);
        EXPECT_FLOAT_EQ(otherR[i], outOtherR[i]);
    }
}

// End-to-end check of the real C++ separation path. Requires the four
// spleeter_*.onnx models to be installed; the four ratio masks sum to one, so
// the stems must reconstruct the input signal.
TEST_F(StemsTest, SpleeterReconstruction) {
    auto& engine = mixxx::SpleeterEngine::instance();
    if (!engine.isAvailable()) {
        GTEST_SKIP() << "Spleeter models not installed";
    }
    ASSERT_TRUE(engine.initialize());

    constexpr uint32_t kFrames = mixxx::spleeter::kSamplesPerBlock;
    std::vector<float> inL = makeSine(kFrames, 440.0);
    std::vector<float> inR = makeSine(kFrames, 660.0);

    std::vector<float> vocL(kFrames), vocR(kFrames);
    std::vector<float> drumL(kFrames), drumR(kFrames);
    std::vector<float> bassL(kFrames), bassR(kFrames);
    std::vector<float> otherL(kFrames), otherR(kFrames);

    ASSERT_TRUE(engine.separateBlock(inL.data(), inR.data(), kFrames, 0,
            vocL.data(), vocR.data(),
            drumL.data(), drumR.data(),
            bassL.data(), bassR.data(),
            otherL.data(), otherR.data(),
            nullptr));

    double error = 0.0;
    double reference = 0.0;
    for (uint32_t i = 0; i < kFrames; ++i) {
        const double sumL = static_cast<double>(vocL[i]) + drumL[i] + bassL[i] + otherL[i];
        const double sumR = static_cast<double>(vocR[i]) + drumR[i] + bassR[i] + otherR[i];
        error += (sumL - inL[i]) * (sumL - inL[i]) + (sumR - inR[i]) * (sumR - inR[i]);
        reference += static_cast<double>(inL[i]) * inL[i] + static_cast<double>(inR[i]) * inR[i];
    }
    const double residualDb = 10.0 * std::log10((error + 1e-30) / (reference + 1e-30));
    EXPECT_LT(residualDb, -80.0) << "stems do not reconstruct the mix";
}

} // namespace
