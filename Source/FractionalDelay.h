#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
namespace tdEffects {
struct FractionalDelay {
    std::vector<float> samples; int write=0;
    void prepare(int size){samples.assign((size_t)std::max(1,size)+4,0);write=0;}
    void reset(){std::fill(samples.begin(),samples.end(),0);write=0;}
    float read(float delay) const {
        if(samples.empty())return 0;
        const int size=(int)samples.size();
        const double d=std::clamp(std::isfinite(delay)?(double)delay:1.0,1.0,(double)size-3.0);
        double pos=std::fmod((double)write-d,(double)size);
        if(pos<0)pos+=(double)size;
        // Explicitly bound both indices, including rounding at the wrap boundary.
        const int index=std::clamp((int)pos,0,size-1);
        const int next=index+1==size?0:index+1;
        const float frac=(float)std::clamp(pos-index,0.0,1.0);
        return samples[(size_t)index]+frac*(samples[(size_t)next]-samples[(size_t)index]);
    }
    void push(float sample){samples[(size_t)write]=sample;if(++write==(int)samples.size())write=0;}
};
}
