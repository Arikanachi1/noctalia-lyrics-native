#include "spectrum.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
int main() {
    int failures=0;
    auto check=[&](bool ok,const char *name){if(!ok){std::cerr<<name<<" FAILED\n";++failures;}};
    SpectrumCaptureRetry retry;
    check(retry.attempt(true,false,100),"first open starts capture");
    check(!retry.attempt(true,true,200),"healthy capture does not reconnect");
    check(!retry.attempt(false,false,300),"closed panel does not capture");
    check(retry.attempt(true,false,400),"reopen bypasses the old five-second connection deadline");
    check(!retry.attempt(true,false,500),"continuous server outage retains connection backoff");
    check(retry.attempt(true,false,5400),"continuous outage retries at its deadline");
    SpectrumAnalyzer analyzer;
    std::array<float,2048> samples{};
    auto silent=analyzer.analyze(samples,48000,.04);
    check(std::all_of(silent.begin(),silent.end(),[](float x){return x==0;}),"silence");
    for (const auto [hz,lo,hi] : {std::array<int,3>{100,2,6}, {1000,15,18}, {8000,26,30}}) {
        analyzer.reset();
        for(size_t i=0;i<samples.size();++i) samples[i]=.5f*std::sin(2*std::numbers::pi*hz*i/48000.);
        auto bands=analyzer.analyze(samples,48000,.04);
        auto peak=std::max_element(bands.begin(),bands.end());
        auto index=std::distance(bands.begin(),peak);
        check(index>=lo && index<=hi && *peak>.4,"frequency selects expected logarithmic band");
        samples.fill(0);
        for(int i=0;i<50;++i) bands=analyzer.analyze(samples,48000,.04);
        check(*std::max_element(bands.begin(),bands.end())<.001,"silence decays without fake motion");
    }
    analyzer.reset();
    for(size_t i=0;i<samples.size();++i) samples[i]=.5f*std::sin(2*std::numbers::pi*1000*i/48000.);
    auto attack=analyzer.analyze(samples,48000,.02);
    check(*std::max_element(attack.begin(),attack.end())>.65,"beat reaches visible height in one 20ms frame");
    samples.fill(0);
    for(int i=0;i<5;++i)attack=analyzer.analyze(samples,48000,.02);
    check(*std::max_element(attack.begin(),attack.end())<.25,"beat releases within 100ms");
    samples.fill(std::numeric_limits<float>::quiet_NaN());samples[0]=std::numeric_limits<float>::infinity();
    auto bands=analyzer.analyze(samples,48000,.04);
    check(std::all_of(bands.begin(),bands.end(),[](float x){return std::isfinite(x)&&x>=0&&x<=1;}),"non-finite input is bounded");
    bands=analyzer.analyze(samples,0,.04);
    check(std::all_of(bands.begin(),bands.end(),[](float x){return x==0;}),"invalid sample rate");
    return failures?1:0;
}
