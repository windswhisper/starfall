#pragma once
#include "Research.h"
#include <GameOptionsClass.h>
#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <vector>
#pragma comment(lib, "winmm.lib")

namespace ResearchAudio {
struct Sample { WAVEFORMATEX format{};std::vector<BYTE> data; };
inline DWORD u32(const BYTE* p) {DWORD v;memcpy(&v,p,4);return v;}
inline WORD u16(const BYTE* p) {WORD v;memcpy(&v,p,2);return v;}
inline bool decode(const std::vector<BYTE>& bytes,Sample& out) {
    if(bytes.size()<12 || memcmp(bytes.data(),"RIFF",4) || memcmp(bytes.data()+8,"WAVE",4)) return false;
    size_t end=static_cast<size_t>(u32(bytes.data()+4))+8;
    if(end>bytes.size() || end<12) return false;
    bool haveFormat=false;const BYTE* samples=nullptr;size_t sampleBytes=0;
    for(size_t pos=12;pos+8<=end;) {
        const BYTE* chunk=bytes.data()+pos;size_t size=u32(chunk+4);pos+=8;
        if(size>end-pos) return false;
        if(!memcmp(chunk,"fmt ",4)) {
            if(size<16) return false;
            auto& f=out.format;f.wFormatTag=u16(bytes.data()+pos);f.nChannels=u16(bytes.data()+pos+2);
            f.nSamplesPerSec=u32(bytes.data()+pos+4);f.nAvgBytesPerSec=u32(bytes.data()+pos+8);
            f.nBlockAlign=u16(bytes.data()+pos+12);f.wBitsPerSample=u16(bytes.data()+pos+14);f.cbSize=0;
            haveFormat=true;
        } else if(!memcmp(chunk,"data",4) && !samples) {samples=bytes.data()+pos;sampleBytes=size;}
        pos+=size;if(size&1) {if(pos>=end) return false;++pos;}
    }
    const auto& f=out.format;
    if(!haveFormat || !samples || !sampleBytes || f.wFormatTag!=WAVE_FORMAT_PCM ||
       (f.nChannels!=1 && f.nChannels!=2) || (f.wBitsPerSample!=8 && f.wBitsPerSample!=16) ||
       f.nSamplesPerSec<8000 || f.nSamplesPerSec>48000 ||
       f.nBlockAlign!=f.nChannels*f.wBitsPerSample/8 || f.nAvgBytesPerSec!=f.nSamplesPerSec*f.nBlockAlign ||
       sampleBytes%f.nBlockAlign) return false;
    out.data.assign(samples,samples+sampleBytes);return true;
}
inline std::wstring widePath(const std::string& path) {
    UINT cp=CP_UTF8;int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,path.c_str(),-1,nullptr,0);
    if(!n) {cp=CP_ACP;n=MultiByteToWideChar(cp,0,path.c_str(),-1,nullptr,0);}
    if(!n) return {};
    std::wstring result(n,L'\0');MultiByteToWideChar(cp,0,path.c_str(),-1,result.data(),n);return result;
}
inline Sample loadBuiltin(int id) {
    Sample sample;HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&loadBuiltin),&module)) return sample;
    auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    if(!resource) return sample;
    DWORD size=SizeofResource(module,resource);
    auto handle=LoadResource(module,resource);
    auto bytes=handle?static_cast<const BYTE*>(LockResource(handle)):nullptr;
    if(bytes && size && size<=16*1024*1024) decode(std::vector<BYTE>(bytes,bytes+size),sample);
    return sample;
}
inline Sample load(const std::string& path) {
    if(!_stricmp(path.c_str(),"builtin:cards")) return loadBuiltin(201);
    if(!_stricmp(path.c_str(),"builtin:select")) return loadBuiltin(202);
    if(!_stricmp(path.c_str(),"builtin:available")) return loadBuiltin(203);
    Sample sample;auto wide=widePath(path);
    HANDLE file=CreateFileW(wide.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return sample;
    LARGE_INTEGER size{};
    if(GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<=16*1024*1024) {
        std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart));DWORD read=0;
        if(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) && read==bytes.size()) decode(bytes,sample);
    }
    CloseHandle(file);return sample;
}
struct Voice {
    HWAVEOUT device=nullptr;WAVEHDR header{};std::vector<BYTE> data;
    ~Voice() {
        if(device) {waveOutReset(device);if(header.dwFlags&WHDR_PREPARED) waveOutUnprepareHeader(device,&header,sizeof(header));waveOutClose(device);}
    }
};
inline std::vector<std::unique_ptr<Voice>>& voices() {static std::vector<std::unique_ptr<Voice>> value;return value;}
inline void pump() {
    auto& active=voices();
    std::erase_if(active,[](const auto& voice){return (voice->header.dwFlags&WHDR_DONE)!=0;});
}
inline bool play(Research::SoundCue cue) {
    pump();const auto& path=Research::soundFiles[static_cast<size_t>(cue)];
    if(path.empty()) return false;
    static std::map<std::string,Sample> cache;
    auto [it,fresh]=cache.try_emplace(path);
    if(fresh) {
        it->second=load(path);
        if(it->second.data.empty()) std::ofstream("Starfall.log",std::ios::app)<<"Starfall: invalid or missing PCM WAV: "<<path<<"\n";
    }
    const auto& sample=it->second;
    if(sample.data.empty()) return false;
    float volume=GameOptionsClass::Instance.SoundVolume;
    if(!std::isfinite(volume)) volume=0;
    volume=std::clamp(volume,0.0f,1.0f);
    if(volume==0) return true;
    if(voices().size()>=8) return true;
    auto voice=std::make_unique<Voice>();voice->data=sample.data;
    if(volume<1.0f) {
        if(sample.format.wBitsPerSample==8) for(auto& v:voice->data) v=static_cast<BYTE>(std::clamp<int>(static_cast<int>(std::lround(128+(int(v)-128)*volume)),0,255));
        else for(size_t i=0;i+1<voice->data.size();i+=2) {
            int16_t value;memcpy(&value,voice->data.data()+i,2);
            value=static_cast<int16_t>(std::lround(value*volume));memcpy(voice->data.data()+i,&value,2);
        }
    }
    if(waveOutOpen(&voice->device,WAVE_MAPPER,&sample.format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) {voice->device=nullptr;return false;}
    voice->header.lpData=reinterpret_cast<LPSTR>(voice->data.data());voice->header.dwBufferLength=static_cast<DWORD>(voice->data.size());
    if(waveOutPrepareHeader(voice->device,&voice->header,sizeof(voice->header))!=MMSYSERR_NOERROR ||
       waveOutWrite(voice->device,&voice->header,sizeof(voice->header))!=MMSYSERR_NOERROR) return false;
    voices().push_back(std::move(voice));return true;
}
}
