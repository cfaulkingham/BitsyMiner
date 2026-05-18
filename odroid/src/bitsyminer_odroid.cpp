/*
 * BitsyMiner Open Source - native ODROID-MC1 Solo build
 * Copyright (c) 2025 Justin Williams
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

constexpr const char* kMinerName = "BitsyMinerOdroid/1.2.0";
constexpr uint32_t kDiff1Bits = 0x1d00ffff;
constexpr long double kDiff1Target =
    26959535291011309493156476344723991336010898738574164086137773096960.0L;

#if defined(__GNUC__) || defined(__clang__)
#define BITSY_ALWAYS_INLINE inline __attribute__((always_inline))
#define BITSY_HOT __attribute__((hot))
#else
#define BITSY_ALWAYS_INLINE inline
#define BITSY_HOT
#endif

using Hash32 = std::array<uint8_t, 32>;
using Header80 = std::array<uint8_t, 80>;

double monotonicSeconds() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    const auto elapsed = clock::now() - start;
    return std::chrono::duration<double>(elapsed).count();
}

uint32_t bswap32(uint32_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(v);
#else
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
#endif
}

uint32_t readBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

void writeBe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

void writeLe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint8_t hexValue(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + c - 'A');
    throw std::runtime_error("invalid hex character");
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    if (hex.size() % 2 != 0) throw std::runtime_error("hex string has odd length");
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>((hexValue(hex[i]) << 4) | hexValue(hex[i + 1])));
    }
    return out;
}

std::string bytesToHex(const uint8_t* data, size_t len) {
    static constexpr char tbl[] = "0123456789abcdef";
    std::string out(len * 2, '0');
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = tbl[data[i] >> 4];
        out[i * 2 + 1] = tbl[data[i] & 0x0f];
    }
    return out;
}

std::string hex32(uint32_t v) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(8) << std::nouppercase << v;
    return os.str();
}

std::string encodeExtraNonce2(size_t len, uint64_t value) {
    static constexpr char tbl[] = "0123456789ABCDEF";
    std::string out(len * 2, '0');
    for (size_t pos = len; pos-- > 0;) {
        out[pos * 2] = tbl[(value >> 4) & 0x0f];
        out[pos * 2 + 1] = tbl[value & 0x0f];
        value >>= 8;
    }
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string formatHashrate(double hashesPerSecond) {
    const char* suffix = "H/s";
    double value = hashesPerSecond;
    if (value >= 1e9) {
        value /= 1e9;
        suffix = "GH/s";
    } else if (value >= 1e6) {
        value /= 1e6;
        suffix = "MH/s";
    } else if (value >= 1e3) {
        value /= 1e3;
        suffix = "kH/s";
    }
    std::ostringstream os;
    os << std::fixed << std::setprecision(value >= 100.0 ? 1 : 2) << value << ' ' << suffix;
    return os.str();
}

namespace sha256 {

constexpr uint32_t kInit[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
};

constexpr uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

BITSY_ALWAYS_INLINE uint32_t rotr(uint32_t v, unsigned shift) {
    return (v >> shift) | (v << (32 - shift));
}

BITSY_ALWAYS_INLINE uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return z ^ (x & (y ^ z));
}

BITSY_ALWAYS_INLINE uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) | (z & (x | y));
}

BITSY_ALWAYS_INLINE uint32_t bigSigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

BITSY_ALWAYS_INLINE uint32_t bigSigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

BITSY_ALWAYS_INLINE uint32_t smallSigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

BITSY_ALWAYS_INLINE uint32_t smallSigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

BITSY_ALWAYS_INLINE void compressWords(uint32_t state[8], const uint32_t first16[16]) {
    uint32_t w[64];
    for (size_t i = 0; i < 16; ++i) w[i] = first16[i];
    for (size_t i = 16; i < 64; ++i) {
        w[i] = w[i - 16] + smallSigma0(w[i - 15]) + w[i - 7] + smallSigma1(w[i - 2]);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (size_t i = 0; i < 64; ++i) {
        const uint32_t t1 = h + bigSigma1(e) + ch(e, f, g) + kRound[i] + w[i];
        const uint32_t t2 = bigSigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void compress(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[16];
    for (size_t i = 0; i < 16; ++i) w[i] = readBe32(block + i * 4);
    compressWords(state, w);
}

Hash32 digest(const uint8_t* data, size_t len) {
    uint32_t state[8];
    std::copy(std::begin(kInit), std::end(kInit), state);

    size_t offset = 0;
    while (len - offset >= 64) {
        compress(state, data + offset);
        offset += 64;
    }

    uint8_t block[128] = {};
    const size_t remain = len - offset;
    if (remain > 0) std::memcpy(block, data + offset, remain);
    block[remain] = 0x80;
    const uint64_t bitLen = static_cast<uint64_t>(len) * 8u;
    const size_t finalLen = remain + 1 > 56 ? 128 : 64;
    for (size_t i = 0; i < 8; ++i) {
        block[finalLen - 1 - i] = static_cast<uint8_t>(bitLen >> (8 * i));
    }
    compress(state, block);
    if (finalLen == 128) compress(state, block + 64);

    Hash32 out{};
    for (size_t i = 0; i < 8; ++i) writeBe32(out.data() + i * 4, state[i]);
    return out;
}

Hash32 doubleDigest(const uint8_t* data, size_t len) {
    const Hash32 first = digest(data, len);
    return digest(first.data(), first.size());
}

std::array<uint32_t, 8> midstate(const Header80& header) {
    std::array<uint32_t, 8> state{};
    std::copy(std::begin(kInit), std::end(kInit), state.begin());
    compress(state.data(), header.data());
    return state;
}

BITSY_ALWAYS_INLINE void doubleHeaderStateWithNonce(
    const std::array<uint32_t, 8>& firstMidstate,
    const std::array<uint32_t, 3>& tailWords,
    uint32_t nonce,
    uint32_t finalState[8]
) {
    uint32_t state[8];
    std::copy(firstMidstate.begin(), firstMidstate.end(), state);

    uint32_t secondChunk[16] = {
        tailWords[0],
        tailWords[1],
        tailWords[2],
        bswap32(nonce),
        0x80000000u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0x00000280u
    };
    compressWords(state, secondChunk);

    uint32_t finalChunk[16] = {
        state[0], state[1], state[2], state[3],
        state[4], state[5], state[6], state[7],
        0x80000000u,
        0u, 0u, 0u, 0u, 0u, 0u,
        0x00000100u
    };

    std::copy(std::begin(kInit), std::end(kInit), finalState);
    compressWords(finalState, finalChunk);
}

Hash32 hashFromState(const uint32_t finalState[8]) {
    Hash32 out{};
    for (size_t i = 0; i < 8; ++i) writeBe32(out.data() + i * 4, finalState[i]);
    return out;
}

BITSY_ALWAYS_INLINE bool stateMeetsTargetWords(
    const uint32_t finalState[8],
    const std::array<uint32_t, 8>& targetWords
) {
    for (int i = 7; i >= 0; --i) {
        const uint32_t hashWord = bswap32(finalState[static_cast<size_t>(i)]);
        const uint32_t targetWord = targetWords[static_cast<size_t>(i)];
        if (hashWord < targetWord) return true;
        if (hashWord > targetWord) return false;
    }
    return true;
}

Hash32 doubleHeaderWithNonce(
    const std::array<uint32_t, 8>& firstMidstate,
    const std::array<uint32_t, 3>& tailWords,
    uint32_t nonce
) {
    uint32_t finalState[8];
    doubleHeaderStateWithNonce(firstMidstate, tailWords, nonce, finalState);
    return hashFromState(finalState);
}

}  // namespace sha256

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    long double number = 0.0;
    std::string str;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    const Json& at(const std::string& key) const {
        static const Json nullValue;
        auto it = object.find(key);
        return it == object.end() ? nullValue : it->second;
    }

    const Json& at(size_t index) const {
        static const Json nullValue;
        return index < array.size() ? array[index] : nullValue;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    Json parse() {
        Json value = parseValue();
        skipWs();
        if (pos_ != input_.size()) throw std::runtime_error("extra JSON input");
        return value;
    }

private:
    const std::string& input_;
    size_t pos_ = 0;

    void skipWs() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }

    bool consume(char expected) {
        skipWs();
        if (pos_ < input_.size() && input_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) throw std::runtime_error("unexpected JSON token");
    }

    Json parseValue() {
        skipWs();
        if (pos_ >= input_.size()) throw std::runtime_error("unexpected end of JSON");
        const char c = input_[pos_];
        if (c == '"') return parseStringValue();
        if (c == '[') return parseArray();
        if (c == '{') return parseObject();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        if (matchLiteral("true")) {
            Json v;
            v.type = Json::Type::Bool;
            v.boolean = true;
            return v;
        }
        if (matchLiteral("false")) {
            Json v;
            v.type = Json::Type::Bool;
            v.boolean = false;
            return v;
        }
        if (matchLiteral("null")) return Json{};
        throw std::runtime_error("invalid JSON value");
    }

    bool matchLiteral(const char* literal) {
        const size_t len = std::strlen(literal);
        if (input_.compare(pos_, len, literal) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    Json parseStringValue() {
        Json v;
        v.type = Json::Type::String;
        v.str = parseString();
        return v;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (pos_ < input_.size()) {
            char c = input_[pos_++];
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= input_.size()) throw std::runtime_error("bad JSON escape");
            c = input_[pos_++];
            switch (c) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    if (pos_ + 4 > input_.size()) throw std::runtime_error("bad unicode escape");
                    out.push_back('?');
                    pos_ += 4;
                    break;
                default:
                    throw std::runtime_error("unknown JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    Json parseArray() {
        Json v;
        v.type = Json::Type::Array;
        expect('[');
        if (consume(']')) return v;
        do {
            v.array.push_back(parseValue());
        } while (consume(','));
        expect(']');
        return v;
    }

    Json parseObject() {
        Json v;
        v.type = Json::Type::Object;
        expect('{');
        if (consume('}')) return v;
        do {
            skipWs();
            const std::string key = parseString();
            expect(':');
            v.object.emplace(key, parseValue());
        } while (consume(','));
        expect('}');
        return v;
    }

    Json parseNumber() {
        const size_t start = pos_;
        if (input_[pos_] == '-') ++pos_;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }

        Json v;
        v.type = Json::Type::Number;
        v.number = std::strtold(input_.substr(start, pos_ - start).c_str(), nullptr);
        return v;
    }
};

struct Options {
    std::string host;
    std::string port;
    std::string wallet;
    std::string password = "x";
    int threads = 4;
    bool useAffinity = true;
    bool selfTest = false;
    int benchmarkSeconds = 0;
    long double suggestDifficulty = 0.0014L;
    std::vector<int> coreList = {4, 5, 6, 7};
};

struct Notify {
    std::string jobId;
    std::string prevHash;
    std::string coinbase1;
    std::string coinbase2;
    std::vector<std::string> merkleBranches;
    std::string version;
    std::string nbits;
    std::string ntime;
    bool cleanJobs = false;
};

struct Submission {
    std::string jobId;
    std::string extraNonce2;
    uint32_t timestamp = 0;
    uint32_t nonce = 0;
    uint32_t flags = 0;
    long double difficulty = 0.0;
};

struct MiningJob {
    uint64_t sequence = 0;
    std::string jobId;
    std::string extraNonce2Hex;
    Header80 header{};
    std::array<uint32_t, 8> midstate{};
    std::array<uint32_t, 3> tailWords{};
    Hash32 poolTarget{};
    std::array<uint32_t, 8> poolTargetWords{};
    Hash32 blockTarget{};
    uint32_t timestamp = 0;
    uint32_t nonceSeed = 0;
};

struct Stats {
    std::atomic<uint64_t> hashes{0};
    std::atomic<uint64_t> sharesFound{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> jobs{0};
    std::mutex bestMutex;
    long double bestDifficulty = 0.0;
};

class MinerState {
public:
    void setJob(std::shared_ptr<const MiningJob> job) {
        {
            std::lock_guard<std::mutex> lock(jobMutex_);
            job_ = std::move(job);
        }
        jobCv_.notify_all();
    }

    void clearJob() {
        setJob(nullptr);
    }

    std::shared_ptr<const MiningJob> currentJob() const {
        std::lock_guard<std::mutex> lock(jobMutex_);
        return job_;
    }

    std::shared_ptr<const MiningJob> waitForJob(const std::atomic<bool>& running) const {
        std::unique_lock<std::mutex> lock(jobMutex_);
        jobCv_.wait_for(lock, std::chrono::milliseconds(250), [&] {
            return !running.load(std::memory_order_relaxed) || job_ != nullptr;
        });
        return job_;
    }

    void pushSubmission(Submission submission) {
        {
            std::lock_guard<std::mutex> lock(submitMutex_);
            submissions_.push_back(std::move(submission));
        }
        submitCv_.notify_one();
    }

    bool popSubmission(Submission& submission) {
        std::lock_guard<std::mutex> lock(submitMutex_);
        if (submissions_.empty()) return false;
        submission = std::move(submissions_.front());
        submissions_.pop_front();
        return true;
    }

private:
    mutable std::mutex jobMutex_;
    mutable std::condition_variable jobCv_;
    std::shared_ptr<const MiningJob> job_;

    std::mutex submitMutex_;
    std::condition_variable submitCv_;
    std::deque<Submission> submissions_;
};

Hash32 compactBitsToTarget(uint32_t bits) {
    const uint32_t exponent = bits >> 24;
    uint32_t mantissa = bits & 0x007fffffu;
    if (bits & 0x00800000u) mantissa |= 0x00800000u;

    Hash32 target{};
    if (exponent <= 3) {
        mantissa >>= 8 * (3 - exponent);
        writeLe32(target.data(), mantissa);
    } else {
        const uint32_t shift = exponent - 3;
        if (shift < target.size()) {
            target[shift] = static_cast<uint8_t>(mantissa);
            if (shift + 1 < target.size()) target[shift + 1] = static_cast<uint8_t>(mantissa >> 8);
            if (shift + 2 < target.size()) target[shift + 2] = static_cast<uint8_t>(mantissa >> 16);
        }
    }
    return target;
}

Hash32 difficultyToTarget(long double difficulty) {
    if (!(difficulty > 0.0L) || !std::isfinite(static_cast<double>(difficulty))) {
        difficulty = 1.0L;
    }

    long double targetValue = kDiff1Target / difficulty;
    Hash32 target{};
    for (size_t i = 0; i < target.size(); ++i) {
        const long double byteValue = std::fmod(targetValue, 256.0L);
        target[i] = static_cast<uint8_t>(std::max<long double>(0.0L, std::floor(byteValue)));
        targetValue = std::floor(targetValue / 256.0L);
    }
    return target;
}

bool hashMeetsTarget(const Hash32& hashBe, const Hash32& targetLe) {
    for (int i = 31; i >= 0; --i) {
        if (hashBe[static_cast<size_t>(i)] < targetLe[static_cast<size_t>(i)]) return true;
        if (hashBe[static_cast<size_t>(i)] > targetLe[static_cast<size_t>(i)]) return false;
    }
    return true;
}

std::array<uint32_t, 8> targetCompareWords(const Hash32& targetLe) {
    std::array<uint32_t, 8> words{};
    for (size_t i = 0; i < words.size(); ++i) {
        const size_t base = i * 4;
        words[i] = (static_cast<uint32_t>(targetLe[base + 3]) << 24) |
                   (static_cast<uint32_t>(targetLe[base + 2]) << 16) |
                   (static_cast<uint32_t>(targetLe[base + 1]) << 8) |
                   static_cast<uint32_t>(targetLe[base]);
    }
    return words;
}

long double difficultyFromHash(const Hash32& hashBe) {
    long double hashValue = 0.0L;
    for (int i = 31; i >= 0; --i) {
        hashValue = hashValue * 256.0L + static_cast<long double>(hashBe[static_cast<size_t>(i)]);
    }
    if (!(hashValue > 0.0L)) return std::numeric_limits<long double>::infinity();
    return kDiff1Target / hashValue;
}

void wordSwap32InPlace(std::vector<uint8_t>& bytes) {
    if (bytes.size() % 4 != 0) throw std::runtime_error("word-swap data is not 32-bit aligned");
    for (size_t i = 0; i < bytes.size(); i += 4) {
        std::swap(bytes[i], bytes[i + 3]);
        std::swap(bytes[i + 1], bytes[i + 2]);
    }
}

std::shared_ptr<MiningJob> buildMiningJob(
    const Notify& notify,
    const std::string& extraNonce1,
    size_t extraNonce2Size,
    uint64_t extraNonce2Value,
    const Hash32& poolTarget,
    uint64_t sequence
) {
    auto job = std::make_shared<MiningJob>();
    job->sequence = sequence;
    job->jobId = notify.jobId;
    job->timestamp = static_cast<uint32_t>(std::strtoul(notify.ntime.c_str(), nullptr, 16));
    job->extraNonce2Hex = encodeExtraNonce2(extraNonce2Size, extraNonce2Value);

    std::vector<uint8_t> coinbase = hexToBytes(notify.coinbase1);
    const std::vector<uint8_t> en1 = hexToBytes(extraNonce1);
    const std::vector<uint8_t> en2 = hexToBytes(job->extraNonce2Hex);
    const std::vector<uint8_t> cb2 = hexToBytes(notify.coinbase2);
    coinbase.insert(coinbase.end(), en1.begin(), en1.end());
    coinbase.insert(coinbase.end(), en2.begin(), en2.end());
    coinbase.insert(coinbase.end(), cb2.begin(), cb2.end());

    Hash32 merkle = sha256::doubleDigest(coinbase.data(), coinbase.size());
    for (const std::string& branchHex : notify.merkleBranches) {
        const std::vector<uint8_t> branch = hexToBytes(branchHex);
        if (branch.size() != 32) throw std::runtime_error("merkle branch entry is not 32 bytes");
        uint8_t pair[64];
        std::memcpy(pair, merkle.data(), 32);
        std::memcpy(pair + 32, branch.data(), 32);
        merkle = sha256::doubleDigest(pair, sizeof(pair));
    }

    const uint32_t version = static_cast<uint32_t>(std::strtoul(notify.version.c_str(), nullptr, 16));
    const uint32_t bits = static_cast<uint32_t>(std::strtoul(notify.nbits.c_str(), nullptr, 16));

    std::vector<uint8_t> prevHash = hexToBytes(notify.prevHash);
    if (prevHash.size() != 32) throw std::runtime_error("previous hash is not 32 bytes");
    wordSwap32InPlace(prevHash);

    writeLe32(job->header.data(), version);
    std::memcpy(job->header.data() + 4, prevHash.data(), 32);
    std::memcpy(job->header.data() + 36, merkle.data(), 32);
    writeLe32(job->header.data() + 68, job->timestamp);
    writeLe32(job->header.data() + 72, bits);
    writeLe32(job->header.data() + 76, 0);

    job->midstate = sha256::midstate(job->header);
    job->tailWords = {
        readBe32(job->header.data() + 64),
        readBe32(job->header.data() + 68),
        readBe32(job->header.data() + 72)
    };
    job->poolTarget = poolTarget;
    job->poolTargetWords = targetCompareWords(poolTarget);
    job->blockTarget = compactBitsToTarget(bits);

    std::random_device rd;
    job->nonceSeed = (static_cast<uint32_t>(rd()) << 16) ^ static_cast<uint32_t>(rd());
    return job;
}

class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient() { close(); }

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool connectTo(const std::string& host, const std::string& port, std::string& error) {
        close();

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;

        const int gai = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
        if (gai != 0) {
            error = gai_strerror(gai);
            return false;
        }

        for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
            fd_ = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd_ < 0) continue;

            if (::connect(fd_, rp->ai_addr, rp->ai_addrlen) == 0) {
                freeaddrinfo(result);
                return true;
            }

            ::close(fd_);
            fd_ = -1;
        }

        error = std::strerror(errno);
        freeaddrinfo(result);
        return false;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        readBuffer_.clear();
    }

    bool connected() const {
        return fd_ >= 0;
    }

    bool sendLine(const std::string& line) {
        if (fd_ < 0) return false;
        const char* data = line.data();
        size_t remaining = line.size();
        while (remaining > 0) {
            const ssize_t sent = ::send(fd_, data, remaining, MSG_NOSIGNAL);
            if (sent <= 0) return false;
            data += sent;
            remaining -= static_cast<size_t>(sent);
        }
        return true;
    }

    bool pollLine(std::string& line, int timeoutMs) {
        line.clear();
        if (extractLine(line)) return true;
        if (fd_ < 0) return false;

        pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, timeoutMs);
        if (rc <= 0) return false;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close();
            return false;
        }

        char buffer[4096];
        const ssize_t got = ::recv(fd_, buffer, sizeof(buffer), 0);
        if (got <= 0) {
            close();
            return false;
        }
        readBuffer_.append(buffer, static_cast<size_t>(got));
        return extractLine(line);
    }

private:
    int fd_ = -1;
    std::string readBuffer_;

    bool extractLine(std::string& line) {
        const size_t pos = readBuffer_.find('\n');
        if (pos == std::string::npos) return false;
        line = readBuffer_.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        readBuffer_.erase(0, pos + 1);
        return true;
    }
};

void parsePoolUrl(const std::string& input, std::string& host, std::string& port) {
    std::string s = input;
    const std::string prefix = "stratum+tcp://";
    if (s.compare(0, prefix.size(), prefix) == 0) s.erase(0, prefix.size());

    const size_t slash = s.find('/');
    if (slash != std::string::npos) s.erase(slash);

    const size_t colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size()) {
        throw std::runtime_error("pool must be host:port or stratum+tcp://host:port");
    }
    host = s.substr(0, colon);
    port = s.substr(colon + 1);
}

std::vector<int> parseCoreList(const std::string& value) {
    std::vector<int> cores;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) cores.push_back(std::stoi(item));
    }
    if (cores.empty()) throw std::runtime_error("core list is empty");
    return cores;
}

void printUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " --pool HOST:PORT --wallet WALLET [options]\n\n"
        << "Options:\n"
        << "  --password PASS             Pool password, default: x\n"
        << "  --threads N                 Mining threads, default: 4\n"
        << "  --core-list 4,5,6,7         CPU affinity list, default: Cortex-A15 cores\n"
        << "  --all-cores                 Use CPUs 0-7 and 8 threads\n"
        << "  --no-affinity               Disable CPU pinning\n"
        << "  --suggest-difficulty N      Send mining.suggest_difficulty, default: 0.0014\n"
        << "  --benchmark SECONDS         Run local SHA-256d benchmark\n"
        << "  --self-test                 Run correctness tests\n";
}

Options parseArgs(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const char* name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(std::string(name) + " requires a value");
            return argv[++i];
        };

        if (arg == "--pool") {
            parsePoolUrl(requireValue("--pool"), opt.host, opt.port);
        } else if (arg == "--wallet") {
            opt.wallet = requireValue("--wallet");
        } else if (arg == "--password") {
            opt.password = requireValue("--password");
        } else if (arg == "--threads") {
            opt.threads = std::max(1, std::stoi(requireValue("--threads")));
        } else if (arg == "--core-list") {
            opt.coreList = parseCoreList(requireValue("--core-list"));
        } else if (arg == "--all-cores") {
            opt.threads = 8;
            opt.coreList = {0, 1, 2, 3, 4, 5, 6, 7};
        } else if (arg == "--no-affinity") {
            opt.useAffinity = false;
        } else if (arg == "--suggest-difficulty") {
            opt.suggestDifficulty = std::strtold(requireValue("--suggest-difficulty").c_str(), nullptr);
        } else if (arg == "--benchmark") {
            opt.benchmarkSeconds = std::max(1, std::stoi(requireValue("--benchmark")));
        } else if (arg == "--self-test") {
            opt.selfTest = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return opt;
}

void pinThreadIfRequested(int workerIndex, const Options& opt) {
    if (!opt.useAffinity || opt.coreList.empty()) return;
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    const int core = opt.coreList[static_cast<size_t>(workerIndex) % opt.coreList.size()];
    CPU_SET(core, &set);
    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        std::cerr << "warning: failed to pin worker " << workerIndex << " to CPU " << core
                  << ": " << std::strerror(rc) << "\n";
    }
#else
    (void)workerIndex;
    (void)opt;
#endif
}

void updateBestDifficulty(Stats& stats, long double difficulty) {
    std::lock_guard<std::mutex> lock(stats.bestMutex);
    if (difficulty > stats.bestDifficulty) stats.bestDifficulty = difficulty;
}

void BITSY_HOT minerWorker(
    int workerIndex,
    int threadCount,
    const Options& opt,
    MinerState& minerState,
    Stats& stats,
    std::atomic<bool>& running,
    std::vector<uint64_t>* workerTotals
) {
    pinThreadIfRequested(workerIndex, opt);
    uint64_t seenSequence = 0;
    uint32_t nonce = static_cast<uint32_t>(workerIndex);
    uint64_t localHashes = 0;
    uint64_t workerHashes = 0;

    while (running.load(std::memory_order_relaxed)) {
        auto job = minerState.currentJob();
        if (!job) {
            stats.hashes.fetch_add(localHashes, std::memory_order_relaxed);
            localHashes = 0;
            job = minerState.waitForJob(running);
            if (!job) continue;
        }

        if (job->sequence != seenSequence) {
            seenSequence = job->sequence;
            nonce = job->nonceSeed + static_cast<uint32_t>(workerIndex);
        }

        for (int batch = 0; batch < 4096 && running.load(std::memory_order_relaxed); ++batch) {
            uint32_t finalState[8];
            sha256::doubleHeaderStateWithNonce(job->midstate, job->tailWords, nonce, finalState);
            ++localHashes;
            ++workerHashes;

            if (sha256::stateMeetsTargetWords(finalState, job->poolTargetWords)) {
                const Hash32 hash = sha256::hashFromState(finalState);
                const long double diff = difficultyFromHash(hash);
                updateBestDifficulty(stats, diff);

                Submission submission;
                submission.jobId = job->jobId;
                submission.extraNonce2 = job->extraNonce2Hex;
                submission.timestamp = job->timestamp;
                submission.nonce = nonce;
                submission.difficulty = diff;
                if (hashMeetsTarget(hash, job->blockTarget)) submission.flags |= 0x04u;
                if (hash[28] == 0 && hash[29] == 0 && hash[30] == 0 && hash[31] == 0) submission.flags |= 0x02u;

                stats.sharesFound.fetch_add(1, std::memory_order_relaxed);
                minerState.pushSubmission(std::move(submission));
            }

            nonce += static_cast<uint32_t>(threadCount);
        }

        stats.hashes.fetch_add(localHashes, std::memory_order_relaxed);
        localHashes = 0;
    }

    stats.hashes.fetch_add(localHashes, std::memory_order_relaxed);
    if (workerTotals && workerIndex >= 0 && static_cast<size_t>(workerIndex) < workerTotals->size()) {
        (*workerTotals)[static_cast<size_t>(workerIndex)] = workerHashes;
    }
}

Notify parseNotify(const Json& root) {
    const Json& params = root.at("params");
    if (!params.isArray() || params.array.size() < 9) {
        throw std::runtime_error("mining.notify params missing fields");
    }

    Notify n;
    n.jobId = params.at(0).str;
    n.prevHash = params.at(1).str;
    n.coinbase1 = params.at(2).str;
    n.coinbase2 = params.at(3).str;
    const Json& branches = params.at(4);
    if (!branches.isArray()) throw std::runtime_error("mining.notify merkle branch is not an array");
    for (const Json& branch : branches.array) n.merkleBranches.push_back(branch.str);
    n.version = params.at(5).str;
    n.nbits = params.at(6).str;
    n.ntime = params.at(7).str;
    n.cleanJobs = params.at(8).isBool() && params.at(8).boolean;
    return n;
}

class StratumSession {
public:
    StratumSession(const Options& options, MinerState& minerState, Stats& stats)
        : options_(options), minerState_(minerState), stats_(stats) {}

    void run(std::atomic<bool>& running) {
        while (running.load(std::memory_order_relaxed)) {
            try {
                runOnce(running);
            } catch (const std::exception& e) {
                minerState_.clearJob();
                std::cerr << "stratum error: " << e.what() << "\n";
            }

            if (running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }

private:
    const Options& options_;
    MinerState& minerState_;
    Stats& stats_;
    TcpClient client_;
    std::string extraNonce1_;
    size_t extraNonce2Size_ = 4;
    uint64_t nextExtraNonce2_ = 1;
    uint64_t rpcId_ = 0;
    uint64_t nextJobSequence_ = 0;
    long double poolDifficulty_ = 1.0L;
    Hash32 poolTarget_ = difficultyToTarget(1.0L);
    std::set<uint64_t> pendingShareIds_;

    uint64_t nextRpcId() {
        if (++rpcId_ == 0) rpcId_ = 1;
        return rpcId_;
    }

    void sendJson(const std::string& line) {
        if (!client_.sendLine(line + "\n")) throw std::runtime_error("socket write failed");
    }

    Json readJson(int timeoutMs) {
        std::string line;
        const double deadline = monotonicSeconds() + timeoutMs / 1000.0;
        while (monotonicSeconds() < deadline) {
            if (client_.pollLine(line, 250)) {
                if (line.empty()) continue;
                return JsonParser(line).parse();
            }
            if (!client_.connected()) throw std::runtime_error("socket disconnected");
        }
        throw std::runtime_error("timeout waiting for stratum response");
    }

    void subscribeAndAuthorize() {
        rpcId_ = 0;
        const uint64_t subscribeId = nextRpcId();
        sendJson("{\"id\":" + std::to_string(subscribeId) +
                 ",\"method\":\"mining.subscribe\",\"params\":[\"" + std::string(kMinerName) + "\"]}");

        while (true) {
            Json response = readJson(15000);
            if (!response.at("id").isNumber() ||
                static_cast<uint64_t>(response.at("id").number) != subscribeId) {
                handleMessage(response);
                continue;
            }
            const Json& result = response.at("result");
            if (!result.isArray() || result.array.size() < 3) {
                throw std::runtime_error("bad mining.subscribe response");
            }
            extraNonce1_ = result.at(1).str;
            extraNonce2Size_ = static_cast<size_t>(result.at(2).number);
            break;
        }

        const uint64_t authId = nextRpcId();
        sendJson("{\"id\":" + std::to_string(authId) +
                 ",\"method\":\"mining.authorize\",\"params\":[\"" +
                 jsonEscape(options_.wallet) + "\",\"" + jsonEscape(options_.password) + "\"]}");

        bool authorized = false;
        while (!authorized) {
            Json response = readJson(15000);
            if (!response.at("id").isNumber() ||
                static_cast<uint64_t>(response.at("id").number) != authId) {
                handleMessage(response);
                continue;
            }
            if (response.at("result").isBool()) authorized = response.at("result").boolean;
            if (!authorized) throw std::runtime_error("pool authorization rejected");
        }

        if (options_.suggestDifficulty > 0.0L) {
            std::ostringstream os;
            os << "{\"id\":" << nextRpcId()
               << ",\"method\":\"mining.suggest_difficulty\",\"params\":["
               << std::setprecision(10) << static_cast<double>(options_.suggestDifficulty) << "]}";
            sendJson(os.str());
        }
    }

    void runOnce(std::atomic<bool>& running) {
        std::string error;
        std::cerr << "connecting to " << options_.host << ":" << options_.port << "\n";
        if (!client_.connectTo(options_.host, options_.port, error)) {
            throw std::runtime_error("connect failed: " + error);
        }

        std::cerr << "connected, subscribing as " << kMinerName << "\n";
        subscribeAndAuthorize();
        std::cerr << "authorized; extranonce1=" << extraNonce1_
                  << " extranonce2_size=" << extraNonce2Size_ << "\n";

        uint64_t lastHashCount = stats_.hashes.load(std::memory_order_relaxed);
        double lastPrint = monotonicSeconds();

        while (running.load(std::memory_order_relaxed) && client_.connected()) {
            std::string line;
            while (client_.pollLine(line, 100)) {
                if (!line.empty()) {
                    Json message = JsonParser(line).parse();
                    handleMessage(message);
                }
            }

            Submission submission;
            while (minerState_.popSubmission(submission)) {
                submitShare(submission);
            }

            const double now = monotonicSeconds();
            if (now - lastPrint >= 5.0) {
                const uint64_t currentHashes = stats_.hashes.load(std::memory_order_relaxed);
                const double rate = static_cast<double>(currentHashes - lastHashCount) / (now - lastPrint);
                lastHashCount = currentHashes;
                lastPrint = now;

                long double best = 0.0L;
                {
                    std::lock_guard<std::mutex> lock(stats_.bestMutex);
                    best = stats_.bestDifficulty;
                }

                std::cerr << "rate=" << formatHashrate(rate)
                          << " total=" << currentHashes
                          << " shares=" << stats_.sharesFound.load(std::memory_order_relaxed)
                          << " accepted=" << stats_.accepted.load(std::memory_order_relaxed)
                          << " rejected=" << stats_.rejected.load(std::memory_order_relaxed)
                          << " best_diff=" << std::setprecision(8) << static_cast<double>(best)
                          << "\n";
            }
        }

        minerState_.clearJob();
        client_.close();
        throw std::runtime_error("pool disconnected");
    }

    void handleMessage(const Json& root) {
        const Json& method = root.at("method");
        if (method.isString()) {
            if (method.str == "mining.set_difficulty") {
                const Json& params = root.at("params");
                if (params.isArray() && params.array.size() > 0 && params.at(0).isNumber()) {
                    poolDifficulty_ = params.at(0).number;
                    poolTarget_ = difficultyToTarget(poolDifficulty_);
                    std::cerr << "pool difficulty set to " << std::setprecision(10)
                              << static_cast<double>(poolDifficulty_) << "\n";
                }
            } else if (method.str == "mining.set_extranonce") {
                const Json& params = root.at("params");
                if (params.isArray() && params.array.size() >= 2 &&
                    params.at(0).isString() && params.at(1).isNumber()) {
                    extraNonce1_ = params.at(0).str;
                    extraNonce2Size_ = static_cast<size_t>(params.at(1).number);
                    minerState_.clearJob();
                    std::cerr << "extranonce updated; extranonce1=" << extraNonce1_
                              << " extranonce2_size=" << extraNonce2Size_ << "\n";
                }
            } else if (method.str == "mining.notify") {
                Notify notify = parseNotify(root);
                if (notify.cleanJobs) {
                    minerState_.clearJob();
                }
                auto job = buildMiningJob(
                    notify,
                    extraNonce1_,
                    extraNonce2Size_,
                    nextExtraNonce2_++,
                    poolTarget_,
                    ++nextJobSequence_
                );
                minerState_.setJob(job);
                stats_.jobs.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "new job " << notify.jobId << " diff="
                          << static_cast<double>(poolDifficulty_) << "\n";
            }
            return;
        }

        if (root.at("id").isNumber() && root.at("result").isBool()) {
            const auto responseId = static_cast<uint64_t>(root.at("id").number);
            if (pendingShareIds_.erase(responseId) == 0) return;
            if (root.at("result").boolean) {
                stats_.accepted.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "share accepted\n";
            } else {
                stats_.rejected.fetch_add(1, std::memory_order_relaxed);
                std::cerr << "share rejected\n";
            }
        }
    }

    void submitShare(const Submission& submission) {
        const uint64_t id = nextRpcId();
        const std::string line =
            "{\"id\":" + std::to_string(id) +
            ",\"method\":\"mining.submit\",\"params\":[\"" + jsonEscape(options_.wallet) +
            "\",\"" + jsonEscape(submission.jobId) +
            "\",\"" + jsonEscape(submission.extraNonce2) +
            "\",\"" + hex32(submission.timestamp) +
            "\",\"" + hex32(submission.nonce) + "\"]}";
        pendingShareIds_.insert(id);
        sendJson(line);

        std::cerr << "submitted share job=" << submission.jobId
                  << " nonce=" << hex32(submission.nonce)
                  << " diff=" << std::setprecision(8) << static_cast<double>(submission.difficulty);
        if (submission.flags & 0x04u) std::cerr << " BLOCK";
        std::cerr << "\n";
    }
};

bool runSelfTest() {
    const std::string abc = "abc";
    const Hash32 abcHash = sha256::digest(reinterpret_cast<const uint8_t*>(abc.data()), abc.size());
    if (bytesToHex(abcHash.data(), abcHash.size()) !=
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        std::cerr << "self-test failed: sha256(abc)\n";
        return false;
    }

    const std::string genesisHeaderHex =
        "01000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "29ab5f49"
        "ffff001d"
        "1dac2b7c";
    const std::vector<uint8_t> genesisHeader = hexToBytes(genesisHeaderHex);
    const Hash32 genesisDigest = sha256::doubleDigest(genesisHeader.data(), genesisHeader.size());
    std::string blockHash(genesisDigest.rbegin(), genesisDigest.rend());
    const std::string blockHashHex = bytesToHex(
        reinterpret_cast<const uint8_t*>(blockHash.data()),
        blockHash.size()
    );
    if (blockHashHex != "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f") {
        std::cerr << "self-test failed: genesis block hash got " << blockHashHex << "\n";
        return false;
    }

    Header80 header{};
    std::copy(genesisHeader.begin(), genesisHeader.end(), header.begin());
    const auto midstate = sha256::midstate(header);
    const std::array<uint32_t, 3> tailWords = {
        readBe32(header.data() + 64),
        readBe32(header.data() + 68),
        readBe32(header.data() + 72)
    };
    uint32_t finalState[8];
    sha256::doubleHeaderStateWithNonce(midstate, tailWords, 0x7c2bac1du, finalState);
    const Hash32 minedDigest = sha256::doubleHeaderWithNonce(midstate, tailWords, 0x7c2bac1du);
    std::string minedBlockHash(minedDigest.rbegin(), minedDigest.rend());
    const std::string minedBlockHashHex = bytesToHex(
        reinterpret_cast<const uint8_t*>(minedBlockHash.data()),
        minedBlockHash.size()
    );
    if (minedBlockHashHex != "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f") {
        std::cerr << "self-test failed: optimized header path got " << minedBlockHashHex << "\n";
        return false;
    }
    const Hash32 diff1Target = compactBitsToTarget(kDiff1Bits);
    if (hashMeetsTarget(minedDigest, diff1Target) !=
        sha256::stateMeetsTargetWords(finalState, targetCompareWords(diff1Target))) {
        std::cerr << "self-test failed: target word comparison\n";
        return false;
    }

    const Json parsed = JsonParser("{\"method\":\"mining.set_difficulty\",\"params\":[0.0014]}").parse();
    if (!parsed.at("method").isString() || parsed.at("method").str != "mining.set_difficulty") {
        std::cerr << "self-test failed: JSON parser\n";
        return false;
    }

    std::cerr << "self-test passed\n";
    return true;
}

int runBenchmark(const Options& opt) {
    Header80 header{};
    const std::vector<uint8_t> genesisHeader = hexToBytes(
        "01000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "29ab5f49"
        "ffff001d"
        "00000000"
    );
    std::copy(genesisHeader.begin(), genesisHeader.end(), header.begin());

    auto job = std::make_shared<MiningJob>();
    job->sequence = 1;
    job->jobId = "benchmark";
    job->header = header;
    job->midstate = sha256::midstate(job->header);
    job->tailWords = {
        readBe32(job->header.data() + 64),
        readBe32(job->header.data() + 68),
        readBe32(job->header.data() + 72)
    };
    job->poolTarget.fill(0x00);
    job->poolTargetWords = targetCompareWords(job->poolTarget);
    job->blockTarget = compactBitsToTarget(kDiff1Bits);
    job->timestamp = 0x495fab29u;

    MinerState minerState;
    Stats stats;
    std::atomic<bool> running{true};
    minerState.setJob(job);

    std::vector<std::thread> workers;
    std::vector<uint64_t> workerTotals(static_cast<size_t>(opt.threads), 0);
    for (int i = 0; i < opt.threads; ++i) {
        workers.emplace_back(
            minerWorker,
            i,
            opt.threads,
            std::cref(opt),
            std::ref(minerState),
            std::ref(stats),
            std::ref(running),
            &workerTotals
        );
    }

    std::this_thread::sleep_for(std::chrono::seconds(opt.benchmarkSeconds));
    running.store(false);
    minerState.clearJob();
    for (auto& worker : workers) worker.join();

    const uint64_t hashes = stats.hashes.load(std::memory_order_relaxed);
    const double rate = static_cast<double>(hashes) / static_cast<double>(opt.benchmarkSeconds);
    std::cerr << "benchmark: " << hashes << " hashes in " << opt.benchmarkSeconds
              << "s, " << formatHashrate(rate) << "\n";
    for (int i = 0; i < opt.threads; ++i) {
        const int core = opt.coreList.empty() ? i : opt.coreList[static_cast<size_t>(i) % opt.coreList.size()];
        const double workerRate = static_cast<double>(workerTotals[static_cast<size_t>(i)]) /
                                  static_cast<double>(opt.benchmarkSeconds);
        std::cerr << "worker " << i;
        if (opt.useAffinity) std::cerr << " cpu=" << core;
        std::cerr << " rate=" << formatHashrate(workerRate)
                  << " hashes=" << workerTotals[static_cast<size_t>(i)] << "\n";
    }
    return 0;
}

std::atomic<bool>* gRunning = nullptr;

void handleSignal(int) {
    if (gRunning) gRunning->store(false);
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    try {
        Options opt = parseArgs(argc, argv);

        if (opt.selfTest) {
            return runSelfTest() ? 0 : 1;
        }

        if (opt.benchmarkSeconds > 0) {
            return runBenchmark(opt);
        }

        if (opt.host.empty() || opt.port.empty() || opt.wallet.empty()) {
            printUsage(argv[0]);
            return 2;
        }

        std::atomic<bool> running{true};
        gRunning = &running;
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        MinerState minerState;
        Stats stats;

        std::cerr << "starting " << opt.threads << " miner thread(s)";
        if (opt.useAffinity) {
            std::cerr << " with CPU affinity";
        }
        std::cerr << "\n";

        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(opt.threads));
        for (int i = 0; i < opt.threads; ++i) {
            workers.emplace_back(
                minerWorker,
                i,
                opt.threads,
                std::cref(opt),
                std::ref(minerState),
                std::ref(stats),
                std::ref(running),
                nullptr
            );
        }

        StratumSession session(opt, minerState, stats);
        session.run(running);

        running.store(false);
        minerState.clearJob();
        for (auto& worker : workers) worker.join();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
