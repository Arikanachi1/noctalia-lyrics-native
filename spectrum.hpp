#pragma once
#include <array>
#include <span>
#include <cstdint>
class SpectrumAnalyzer {
public:
    static constexpr size_t windowSize=2048, bandCount=32;
private:
    std::array<float,bandCount> previous{};
public:
    void reset() { previous.fill(0); }
    std::array<float,bandCount> analyze(std::span<const float> samples, double rate, double dt);
};

// Connection backoff is independent of the audio server and display lifetime.
class SpectrumCaptureRetry {
    int64_t retryAt=0;
public:
    bool attempt(bool wanted,bool healthy,int64_t now) {
        if(!wanted){retryAt=0;return false;}
        if(healthy || now<retryAt)return false;
        retryAt=now+5000;
        return true;
    }
};
