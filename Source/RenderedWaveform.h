#pragma once
#include "RenderedPreview.h"

class RenderedWaveform : public juce::Component, public juce::FileDragAndDropTarget, private juce::ScrollBar::Listener {
public:
    explicit RenderedWaveform(tdPreview::Session& session):session(&session),scroll(false) {
        addAndMakeVisible(scroll);scroll.addListener(this);
        setMouseCursor(juce::MouseCursor::IBeamCursor);
    }
    std::function<void(const juce::File&)> fileDropped;
    std::function<void()> seeked;
    std::function<bool()> canDrop;
    bool canExport=true;
    void bind(tdPreview::Session& next,bool exportFile){if(session!=&next){session=&next;resetView();}canExport=exportFile;repaint();}
    static bool supported(const juce::String& path){return juce::StringArray{".wav",".flac",".aif",".aiff",".mp3",".m4a",".aac",".ogg",".opus"}.contains(juce::File(path).getFileExtension().toLowerCase());}
    bool isInterestedInFileDrag(const juce::StringArray& files) override {return (!canDrop || canDrop()) && files.size()==1 && supported(files[0]);}
    void fileDragEnter(const juce::StringArray&,int,int) override {dropHover=true;repaint();}
    void fileDragExit(const juce::StringArray&) override {dropHover=false;repaint();}
    void filesDropped(const juce::StringArray& files,int,int) override {dropHover=false;repaint();if(isInterestedInFileDrag(files) && fileDropped)fileDropped(juce::File(files[0]));}
    void mouseDoubleClick(const juce::MouseEvent&) override {resetView();}
    void resetView(){viewStart=0;viewLength=duration();refreshScroll();repaint();}
    void refresh(){refreshScroll();repaint();}
    double visibleDuration() const {return viewLength;}
    double startTime() const {return viewStart;}
    void zoom(double factor,double anchor) {
        if(duration()<=0)return;
        const double before=viewLength>0?viewLength:duration();
        const double after=juce::jlimit(juce::jmin(.02,duration()),duration(),before/factor);
        anchor=juce::jlimit(0.,1.,anchor);viewStart=juce::jlimit(0.,duration()-after,viewStart+anchor*(before-after));viewLength=after;refresh();
    }
    void resized() override {scroll.setBounds(12,getHeight()-17,getWidth()-24,9);refreshScroll();}
    void paint(juce::Graphics& g) override {
        const auto gold=juce::Colour(0xffe9b66a);auto bounds=getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff141d21));g.fillRoundedRectangle(bounds,7);
        g.setColour(gold.withAlpha(dropHover?.9f:.26f));g.drawRoundedRectangle(bounds.reduced(1.f),7,dropHover?2.f:1.f);
        const auto* data=session->audio();if(!data){g.setColour(gold.withAlpha(.8f));g.setFont(15);g.drawText("Drop audio here from Explorer or a compatible DAW",getLocalBounds().reduced(16),juce::Justification::centred);return;}
        const auto track=trackBounds();if(track.getWidth()<=0 || viewLength<=0)return;
        // Rulers share the waveform coordinates, so seeking and zooming remain exact.
        const double rawStep=viewLength/7.;const double decade=std::pow(10.,std::floor(std::log10(rawStep)));
        const double nice=rawStep/decade;const double tick=(nice<=1?1:nice<=2?2:nice<=5?5:10)*decade;
        g.setFont(10.f);
        for(double t=std::ceil(viewStart/tick)*tick;t<=viewStart+viewLength+.000001;t+=tick) {
            const float x=track.getX()+(float)((t-viewStart)/viewLength)*track.getWidth();
            g.setColour(gold.withAlpha(.10f));g.drawVerticalLine(juce::roundToInt(x),track.getY(),track.getBottom());
            g.setColour(gold.withAlpha(.7f));g.drawLine(x,20,x,24);g.drawLine(x,track.getBottom(),x,track.getBottom()+4);
            g.drawText(time(t,tick<1),juce::jlimit(12,getWidth()-74,juce::roundToInt(x)+3),3,70,16,juce::Justification::centredLeft);
        }
        for(int c=0;c<data->samples.getNumChannels();++c) {
            const float lane=track.getHeight()/data->samples.getNumChannels(),centre=track.getY()+lane*(c+.5f),amplitude=lane*.43f;
            g.setColour(gold.withAlpha(.15f));g.drawHorizontalLine(juce::roundToInt(centre),track.getX(),track.getRight());
            juce::Path path;path.preallocateSpace((int)track.getWidth()*12);
            for(int x=0;x<(int)track.getWidth();++x) {
                const double begin=(viewStart+viewLength*x/track.getWidth())*data->sampleRate;
                const double end=(viewStart+viewLength*(x+1)/track.getWidth())*data->sampleRate;
                const auto p=data->range(c,(int)begin,juce::jmax((int)begin+1,(int)std::ceil(end)));
                path.startNewSubPath(track.getX()+x,centre-juce::jlimit(-1.f,1.f,p.high)*amplitude);
                path.lineTo(track.getX()+x,centre-juce::jlimit(-1.f,1.f,p.low)*amplitude);
            }
            g.setColour(gold.withAlpha(.9f));g.strokePath(path,juce::PathStrokeType(1.f));
        }
        const double position=session->position.load();
        if(position>=viewStart && position<=viewStart+viewLength) {
            const float x=track.getX()+(float)((position-viewStart)/viewLength)*track.getWidth();g.setColour(juce::Colour(0xffffe2ac));g.drawLine(x,22,x,track.getBottom()+3,1.5f);
            juce::Path head;head.addTriangle(x-4,21,x+4,21,x,26);g.fillPath(head);
        }
        g.setFont(10.f);g.setColour(gold.withAlpha(.65f));
        g.drawText(time(viewStart,true)+"  -  "+time(viewStart+viewLength,true),12,getHeight()-37,220,16,juce::Justification::centredLeft);
        g.drawText(canExport?"Drag: into DAW | Ctrl+wheel: zoom | Double-click: show all":"Click/drag: seek | Ctrl+wheel: zoom | Double-click: show all",230,getHeight()-37,getWidth()-242,16,juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent&) override {dragged=false;}
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!canExport){seekAt(e.position);return;}
        if(!dragged && e.getDistanceFromDragStart()>6) {
            if(const auto lease=session->snapshot();lease && lease->file.existsAsFile()) {
                const auto* data=lease.get();
                dragged=true;
                const bool retained=data->dragged.exchange(true);
                if(!juce::DragAndDropContainer::performExternalDragDropOfFiles({data->file.getFullPathName()},false,this))data->dragged.store(retained);
            }
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if(!dragged)seekAt(e.position);
    }
    void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel) override {
        if(e.mods.isCtrlDown())zoom(std::exp(wheel.deltaY*3.),(e.position.x-trackBounds().getX())/trackBounds().getWidth());
        else {const double delta=wheel.deltaX!=0?wheel.deltaX:wheel.deltaY;viewStart=juce::jlimit(0.,juce::jmax(0.,duration()-viewLength),viewStart-delta*viewLength*.4);refresh();}
    }
    static juce::String time(double seconds,bool precise=false) {
        const int minutes=(int)seconds/60;const auto rest=seconds-minutes*60;
        return juce::String(minutes)+":"+(rest<10?"0":"")+juce::String(rest,precise?2:0);
    }
private:
    tdPreview::Session* session;juce::ScrollBar scroll;double viewStart=0,viewLength=0;bool dragged=false,dropHover=false;
    void seekAt(juce::Point<float> point){const auto r=trackBounds();if(r.contains(point)){session->seek(viewStart+viewLength*juce::jlimit(0.,1.,(double)(point.x-r.getX())/r.getWidth()));if(seeked)seeked();repaint();}}
    double duration() const {const auto* d=session->audio();return d?d->duration():0.;}
    juce::Rectangle<float> trackBounds() const {return getLocalBounds().toFloat().reduced(12,0).withY(25).withHeight(juce::jmax(0.f,getHeight()-65.f));}
    void refreshScroll(){if(viewLength<=0 && duration()>0)viewLength=duration();scroll.setRangeLimits(0,juce::jmax(.001,duration()));scroll.setCurrentRange(viewStart,juce::jmax(.001,viewLength),juce::dontSendNotification);}
    void scrollBarMoved(juce::ScrollBar*,double start) override {viewStart=start;repaint();}
};
