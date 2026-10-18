#pragma once
#include <JuceHeader.h>
namespace tdUi {
inline void populate(juce::ComboBox& box){box.addItem("Auto",1);for(int percent=50;percent<=200;percent+=25)box.addItem(juce::String(percent)+"%",percent);}
inline float requested(int id){return id>=50 && id<=200?id*.01f:1.0f;}
inline float automatic(juce::Rectangle<int> area){return juce::jlimit(1.0f,2.0f,juce::jmin(area.getWidth()/1920.0f,area.getHeight()/1080.0f));}
inline int valid(int id){return id==1 || (id>=50 && id<=200 && id%25==0)?id:1;}
inline float fit(int id,int width,int height,juce::Rectangle<int> area,int marginX=40,int marginY=72){return juce::jmax(.1f,juce::jmin(id==1?automatic(area):requested(id),juce::jmin((area.getWidth()-marginX)/(float)width,(area.getHeight()-marginY)/(float)height)));}
}
