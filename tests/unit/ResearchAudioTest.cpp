#include "ResearchAudio.h"
#include <cassert>
#include <cstdio>

static void word(std::vector<BYTE>& b,DWORD n) {for(int i=0;i<4;++i)b.push_back(static_cast<BYTE>(n>>(i*8)));}
static void half(std::vector<BYTE>& b,WORD n) {b.push_back(static_cast<BYTE>(n));b.push_back(static_cast<BYTE>(n>>8));}
static void tag(std::vector<BYTE>& b,const char* p) {b.insert(b.end(),p,p+4);}
int main() {
    std::vector<BYTE> wav;tag(wav,"RIFF");word(wav,40);tag(wav,"WAVE");
    tag(wav,"fmt ");word(wav,16);half(wav,1);half(wav,1);word(wav,22050);word(wav,44100);half(wav,2);half(wav,16);
    tag(wav,"data");word(wav,4);half(wav,0);half(wav,1234);
    ResearchAudio::Sample valid;
    assert(ResearchAudio::decode(wav,valid) && valid.format.nSamplesPerSec==22050 && valid.data.size()==4);
    auto broken=wav;broken[20]=3;ResearchAudio::Sample compressed;assert(!ResearchAudio::decode(broken,compressed));
    broken=wav;broken.resize(40);ResearchAudio::Sample truncated;assert(!ResearchAudio::decode(broken,truncated));
    broken=wav;broken[32]=4;ResearchAudio::Sample wrongAlignment;assert(!ResearchAudio::decode(broken,wrongAlignment));
    assert(ResearchAudio::load("no-such-starfall-sound.wav").data.empty());
    for(auto [name,path]:{std::pair{"builtin:cards","assets\\sound\\Cards.wav"},
                           std::pair{"builtin:select","assets\\sound\\Select.wav"},
                           std::pair{"builtin:available","assets\\sound\\Available.wav"}}) {
        auto builtin=ResearchAudio::load(name),file=ResearchAudio::load(path);
        assert(!builtin.data.empty() && builtin.data==file.data &&
            builtin.format.nSamplesPerSec==file.format.nSamplesPerSec && builtin.format.nChannels==file.format.nChannels);
    }
    assert(ResearchAudio::load("BUILTIN:CARDS").data==ResearchAudio::load("builtin:cards").data);
    ResearchAudio::clearCache();
    auto held=ResearchAudio::sampleFor("builtin:cards");
    assert(held==ResearchAudio::sampleFor("BUILTIN:CARDS"));
    // Exercise real WAV reloads at the same path; a cached missing file must also
    // become usable after configuration invalidation.
    const char* temporary="build/tests/unit/cache-reload.wav";
    std::remove(temporary);
    assert(ResearchAudio::sampleFor(temporary)->data.empty());
    {std::ofstream out(temporary,std::ios::binary);out.write(reinterpret_cast<const char*>(wav.data()),wav.size());}
    assert(ResearchAudio::sampleFor(temporary)->data.empty());
    ResearchAudio::clearCache();
    auto first=ResearchAudio::sampleFor(temporary);
    assert(first->data==valid.data);
    auto changed=wav;changed.back()^=0x40;
    {std::ofstream out(temporary,std::ios::binary);out.write(reinterpret_cast<const char*>(changed.data()),changed.size());}
    ResearchAudio::clearCache();
    assert(ResearchAudio::sampleFor(temporary)->data!=first->data && first->data==valid.data);
    std::remove(temporary);
    for(int i=0;i<80;++i) {
        // Cached PCM payloads, no audio device required for capacity validation.
        auto sample=ResearchAudio::sampleCache().get("generated-"+std::to_string(i),[] {
            ResearchAudio::Sample sample;sample.data.resize(1024*1024);return sample;
        },[](const auto& key,const auto& sample){return sample.data.capacity()+2*(key.size()+1)+sizeof(sample);});
        assert(sample->data.size()==1024*1024 && ResearchAudio::sampleCache().bytes()<=ResearchAudio::sampleCache().maxBytes);
    }
    assert(!ResearchAudio::sampleCache().contains("generated-0") && !held->data.empty());
    ResearchAudio::clearCache();
    assert(ResearchAudio::sampleCache().size()==0 && ResearchAudio::sampleCache().bytes()==0);
    puts("PASS: PCM WAV, embedded and external audio, unsupported format, truncated data, alignment and missing file");
    puts("PASS audio cache capacity, aliases, missing-file retry, replaced-file reload and held sample lifetime");
}
