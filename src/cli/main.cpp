// ApoLite - command line tool: offline processing and config validation.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "apolite/apolite.h"
#include "apolite/engine.h"
#include "apolite/mathutil.h"

namespace {

struct Options {
    std::string config;
    double rate = 48000.0;
    size_t channels = 2;
    std::string stage = "post-mix";
    std::string device;
    std::string wavIn;
    std::string wavOut;
    double freq = 1000.0;
    double levelDb = -30.0;  // test signal level; -30 dBFS approximates speech
};

void printUsage() {
    std::printf(
        "ApoLite %s - a lightweight system-wide equalizer for Windows 11\n"
        "\n"
        "Usage:\n"
        "  apolite check <config.txt> [options]\n"
        "  apolite gain <config.txt> --freq HZ [options]\n"
        "  apolite process <config.txt> --wav in.wav out.wav [options]\n"
        "  apolite version\n"
        "\n"
        "Options:\n"
        "  --rate N          sample rate in Hz (default 48000)\n"
        "  --channels N      channel count (default 2)\n"
        "  --stage NAME      capture | pre-mix | post-mix (default post-mix)\n"
        "  --device NAME     device name used for Device: matching\n"
        "  --freq HZ         frequency for the gain command (default 1000)\n"
        "  --level DB        test signal level for gain, in dBFS (default -30)\n"
        "  --wav IN OUT      read and write 16-bit or 32-bit float WAV files\n",
        APOLITE_VERSION_STRING);
}

bool parseOptions(int argc, char** argv, Options& o) {
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) -> bool {
            if (i + 1 >= argc) return false;
            value = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--rate" && next(value)) {
            o.rate = std::strtod(value.c_str(), nullptr);
        } else if (arg == "--channels" && next(value)) {
            o.channels = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (arg == "--stage" && next(value)) {
            o.stage = value;
        } else if (arg == "--device" && next(value)) {
            o.device = value;
        } else if (arg == "--freq" && next(value)) {
            o.freq = std::strtod(value.c_str(), nullptr);
        } else if (arg == "--level" && next(value)) {
            o.levelDb = std::strtod(value.c_str(), nullptr);
        } else if (arg == "--wav" && next(value)) {
            o.wavIn = value;
            if (!next(value)) return false;
            o.wavOut = value;
        } else {
            std::printf("Unknown option: %s\n", arg.c_str());
            return false;
        }
    }
    if (o.channels == 0) o.channels = 1;
    return true;
}

// Engine holds an atomic and a mutex, so it is neither copyable nor movable.
// Configure the caller's instance in place.
void configureEngine(apolite::Engine& engine, const Options& o) {
    engine.setLogSink([](const std::string& msg) { std::printf("  %s\n", msg.c_str()); });

    const bool capture = o.stage == "capture";
    const bool preMix = o.stage == "pre-mix";
    const std::string device = o.device.empty() ? "Speakers" : o.device;
    engine.configure(o.rate, o.channels, o.channels, 0, capture, preMix,
                     /*postMixInstalled=*/true, device, device, "");
}

// ------------------------------------------------------------------- WAV I/O

struct WavData {
    uint32_t rate = 48000;
    uint16_t channels = 2;
    std::vector<float> samples;  // interleaved
};

bool readWav(const std::string& path, WavData& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        std::printf("Cannot open %s\n", path.c_str());
        return false;
    }

    char riff[4], wave[4];
    uint32_t size = 0;
    if (std::fread(riff, 1, 4, f) != 4 || std::fread(&size, 4, 1, f) != 1 ||
        std::fread(wave, 1, 4, f) != 4 || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(wave, "WAVE", 4) != 0) {
        std::printf("%s is not a WAV file\n", path.c_str());
        std::fclose(f);
        return false;
    }

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> data;

    while (true) {
        char id[4];
        uint32_t chunkSize = 0;
        if (std::fread(id, 1, 4, f) != 4 || std::fread(&chunkSize, 4, 1, f) != 1) break;

        if (std::memcmp(id, "fmt ", 4) == 0) {
            uint16_t blockAlign = 0;
            uint32_t byteRate = 0;
            std::fread(&format, 2, 1, f);
            std::fread(&channels, 2, 1, f);
            std::fread(&rate, 4, 1, f);
            std::fread(&byteRate, 4, 1, f);
            std::fread(&blockAlign, 2, 1, f);
            std::fread(&bits, 2, 1, f);
            if (chunkSize > 16) std::fseek(f, static_cast<long>(chunkSize) - 16, SEEK_CUR);
        } else if (std::memcmp(id, "data", 4) == 0) {
            data.resize(chunkSize);
            const size_t read = std::fread(data.data(), 1, chunkSize, f);
            data.resize(read);
            break;
        } else {
            std::fseek(f, static_cast<long>(chunkSize + (chunkSize & 1)), SEEK_CUR);
        }
    }
    std::fclose(f);

    if (channels == 0 || data.empty() || bits == 0) {
        std::printf("%s has no readable audio data\n", path.c_str());
        return false;
    }

    out.rate = rate;
    out.channels = channels;
    const size_t frames = data.size() / (channels * (bits / 8));
    out.samples.resize(frames * channels);

    if (format == 3 && bits == 32) {  // IEEE float
        std::memcpy(out.samples.data(), data.data(), out.samples.size() * sizeof(float));
    } else if (bits == 16) {
        const auto* pcm = reinterpret_cast<const int16_t*>(data.data());
        for (size_t i = 0; i < out.samples.size(); ++i) {
            out.samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
        }
    } else {
        std::printf("Unsupported WAV format: %u-bit\n", bits);
        return false;
    }
    return true;
}

bool writeWav(const std::string& path, const WavData& in) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::printf("Cannot write %s\n", path.c_str());
        return false;
    }

    const uint16_t bits = 32;
    const uint16_t format = 3;  // IEEE float
    const uint32_t dataBytes = static_cast<uint32_t>(in.samples.size() * sizeof(float));

    std::fwrite("RIFF", 1, 4, f);
    const uint32_t riffSize = 36 + dataBytes;
    std::fwrite(&riffSize, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    const uint32_t fmtSize = 16;
    std::fwrite(&fmtSize, 4, 1, f);
    std::fwrite(&format, 2, 1, f);
    std::fwrite(&in.channels, 2, 1, f);
    std::fwrite(&in.rate, 4, 1, f);
    const uint32_t byteRate = in.rate * in.channels * (bits / 8);
    std::fwrite(&byteRate, 4, 1, f);
    const uint16_t blockAlign = static_cast<uint16_t>(in.channels * (bits / 8));
    std::fwrite(&blockAlign, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataBytes, 4, 1, f);
    std::fwrite(in.samples.data(), 1, dataBytes, f);
    std::fclose(f);
    return true;
}

// -------------------------------------------------------------------- commands

int cmdCheck(const Options& o) {
    apolite::Engine engine;
    configureEngine(engine, o);
    const size_t count = engine.loadConfiguration(o.config);
    std::printf("OK: %zu filters loaded for stage %s\n", count, o.stage.c_str());
    return 0;
}

int cmdGain(const Options& o) {
    apolite::Engine engine;
    configureEngine(engine, o);
    engine.loadConfiguration(o.config);

    // Feed a steady sine and measure the response once the chain has settled.
    const double amplitude = std::pow(10.0, o.levelDb / 20.0);
    const size_t frames = static_cast<size_t>(o.rate);  // one second
    std::vector<float> buffer(frames * o.channels);
    for (size_t i = 0; i < frames; ++i) {
        const float v =
            static_cast<float>(amplitude * std::sin(apolite::kTwoPi * o.freq * i / o.rate));
        for (size_t c = 0; c < o.channels; ++c) buffer[i * o.channels + c] = v;
    }
    engine.processInterleaved(buffer.data(), frames);

    // Compare the RMS of the last quarter against the input's.
    const size_t start = frames * 3 / 4;
    double inSum = 0.0, outSum = 0.0;
    for (size_t i = start; i < frames; ++i) {
        const double in = amplitude * std::sin(apolite::kTwoPi * o.freq * i / o.rate);
        inSum += in * in;
        const double out = static_cast<double>(buffer[i * o.channels]);
        outSum += out * out;
    }
    const double ratio = std::sqrt(outSum / inSum);
    const double db = 20.0 * std::log10(ratio > 0.0 ? ratio : 1e-12);
    std::printf("%.1f Hz at %.0f dBFS: %+.2f dB\n", o.freq, o.levelDb, db);
    return 0;
}

int cmdProcess(const Options& o) {
    if (o.wavIn.empty() || o.wavOut.empty()) {
        std::printf("process requires --wav <in> <out>\n");
        return 1;
    }

    WavData wav;
    if (!readWav(o.wavIn, wav)) return 1;

    Options wavOptions = o;
    wavOptions.rate = wav.rate;
    wavOptions.channels = wav.channels;
    apolite::Engine engine;
    configureEngine(engine, wavOptions);
    engine.loadConfiguration(o.config);

    const size_t frames = wav.samples.size() / wav.channels;
    engine.processInterleaved(wav.samples.data(), frames);

    if (!writeWav(o.wavOut, wav)) return 1;
    std::printf("Wrote %zu frames at %u Hz to %s\n", frames, wav.rate, o.wavOut.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string cmd = argv[1];
    if (cmd == "version") {
        std::printf("ApoLite %s\n", APOLITE_VERSION_STRING);
        return 0;
    }

    if (argc < 3) {
        printUsage();
        return 1;
    }

    Options options;
    options.config = argv[2];
    if (!parseOptions(argc, argv, options)) {
        printUsage();
        return 1;
    }

    if (cmd == "check") return cmdCheck(options);
    if (cmd == "gain") return cmdGain(options);
    if (cmd == "process") return cmdProcess(options);

    printUsage();
    return 1;
}
