#include "spectrum.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

std::array<float,SpectrumAnalyzer::bandCount> SpectrumAnalyzer::analyze(std::span<const float> samples,double rate,double dt) {
    if(samples.size()!=windowSize || !std::isfinite(rate) || rate<1000) {reset();return previous;}
    std::array<std::complex<double>,windowSize> fft{};
    for(size_t i=0;i<windowSize;++i) {
        const double value=std::isfinite(samples[i])?std::clamp<double>(samples[i],-1,1):0;
        fft[i]=value*(.5-.5*std::cos(2*std::numbers::pi*i/(windowSize-1)));
    }
    for(size_t i=1,j=0;i<windowSize;++i) {
        size_t bit=windowSize>>1;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;if(i<j)std::swap(fft[i],fft[j]);
    }
    for(size_t len=2;len<=windowSize;len<<=1) {
        const auto step=std::polar(1.,-2*std::numbers::pi/len);
        for(size_t start=0;start<windowSize;start+=len) {
            std::complex<double> phase=1;
            for(size_t i=0;i<len/2;++i) {
                auto a=fft[start+i],b=fft[start+i+len/2]*phase;
                fft[start+i]=a+b;fft[start+i+len/2]=a-b;phase*=step;
            }
        }
    }
    const double upper=std::min(16000.,rate*.48);
    dt=std::isfinite(dt)?std::clamp(dt,0.,1.):.04;
    for(size_t band=0;band<bandCount;++band) {
        const double lo=50*std::pow(upper/50,double(band)/bandCount);
        const double hi=50*std::pow(upper/50,double(band+1)/bandCount);
        const auto first=std::clamp<size_t>(size_t(std::ceil(lo*windowSize/rate)),1,windowSize/2-1);
        const auto last=std::clamp<size_t>(size_t(std::ceil(hi*windowSize/rate)),first+1,windowSize/2);
        double magnitude=0;
        for(size_t bin=first;bin<last;++bin) magnitude=std::max(magnitude,std::abs(fft[bin])*4/windowSize);
        const double level=std::clamp((20*std::log10(std::max(1e-9,magnitude))+72)/66,0.,1.);
        const double tau=level>previous[band]?.012:.06;
        previous[band]+=float((level-previous[band])*(1-std::exp(-dt/tau)));
    }
    return previous;
}
