#include "FractionalDelay.h"
#include <iostream>
#include <limits>
#include <stdexcept>
int main(){
 for(double rate:{44100.0,48000.0,96000.0}){
  tdEffects::FractionalDelay line;line.prepare((int)(rate*0.32));
  const float delay=1+20*(float)rate*0.001f;
  for(size_t i=0;i<line.samples.size();++i)line.samples[i]=(float)i;
  line.write=(int)delay;
  const double expectedPos=(double)line.write-delay+line.samples.size();
  const int a=(int)expectedPos;const int b=(a+1)%(int)line.samples.size();
  const float expected=line.samples[a]+(float)(expectedPos-a)*(line.samples[b]-line.samples[a]);
  if(std::abs(line.read(delay)-expected)>0.00001f)throw std::runtime_error("20ms wrap mismatch");
  for(int w=0;w<(int)line.samples.size();++w){line.write=w;for(float d:{1.0f,delay,200*(float)rate*.001f,(float)line.samples.size(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})if(!std::isfinite(line.read(d)))throw std::runtime_error("invalid interpolation");}
  line.reset();line.push(1);if(line.read(1)!=1)throw std::runtime_error("unit delay");
  std::cout<<"PASS delay boundary "<<rate<<"\n";
 }
}
