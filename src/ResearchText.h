#pragma once
#include <Drawing.h>
#include <map>
#include <list>
#include <string>
#include <vector>
#pragma comment(lib, "gdi32.lib")

namespace ResearchText {
// The game's bitmap font only contains a subset of Chinese characters.
// Cache monochrome coverage from the Windows UI font, then tint it on the current game surface.
struct Mask { int w=0,h=0; std::vector<BYTE> alpha; };
inline Mask raster(const wchar_t* text,int size) {
    Mask result;
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc) return result;
    HFONT font=CreateFontW(-size,0,0,0,size>=18?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,
        GB2312_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Microsoft YaHei UI");
    auto previous=SelectObject(dc,font);
    SIZE bounds{};
    int length=static_cast<int>(wcslen(text));
    if(GetTextExtentPoint32W(dc,text,length,&bounds) && bounds.cx>0 && bounds.cy>0) {
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.cx;
        info.bmiHeader.biHeight=-bounds.cy;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        DWORD* bits=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&bits),nullptr,0);
        if(bitmap && bits) {
            auto oldBitmap=SelectObject(dc,bitmap);
            PatBlt(dc,0,0,bounds.cx,bounds.cy,BLACKNESS);
            SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
            TextOutW(dc,0,0,text,length);GdiFlush();
            result.w=bounds.cx;result.h=bounds.cy;result.alpha.reserve(result.w*result.h);
            for(int i=0;i<result.w*result.h;++i) result.alpha.push_back(static_cast<BYTE>(bits[i]));
            SelectObject(dc,oldBitmap);
        }
        if(bitmap) DeleteObject(bitmap);
    }
    SelectObject(dc,previous);if(font) DeleteObject(font);DeleteDC(dc);
    return result;
}
struct Cache {
    using Key=std::pair<int,std::wstring>;
    struct Entry { Mask mask;std::list<Key>::iterator recent;size_t bytes; };
    static constexpr size_t maxEntries=256,maxBytes=4*1024*1024;
    std::map<Key,Entry> entries;
    std::list<Key> recent;
    size_t bytes=0;
    const Mask& get(const wchar_t* text,int size) {
        Key key{size,text};
        if(auto it=entries.find(key);it!=entries.end()) {
            recent.splice(recent.begin(),recent,it->second.recent);
            return it->second.mask;
        }
        auto mask=raster(text,size);
        size_t cost=mask.alpha.size()+2*(key.second.size()+1)*sizeof(wchar_t);
        // Oversized or failed rasterization uses draw()'s native-font fallback.
        if(mask.alpha.empty() || cost>maxBytes) {static const Mask empty;return empty;}
        while(entries.size()>=maxEntries || bytes+cost>maxBytes) {
            auto oldest=entries.find(recent.back());
            bytes-=oldest->second.bytes;entries.erase(oldest);recent.pop_back();
        }
        recent.push_front(key);
        auto [it,added]=entries.emplace(std::move(key),Entry{std::move(mask),recent.begin(),cost});
        bytes+=cost;return it->second.mask;
    }
};
inline const Mask& maskFor(const wchar_t* text,int size) {
    static Cache cache;
    return cache.get(text,size);
}
inline void draw(const wchar_t* text,int x,int y,int color,int size=15,int maxWidth=INT_MAX) {
    const Mask& mask=maskFor(text,size);
    auto surface=DSurface::Composite;
    if(mask.alpha.empty() || surface->GetBytesPerPixel()!=2) { surface->DrawText(text,x,y,color); return; }
    int width=std::min({mask.w,surface->GetWidth()-x,maxWidth}),height=std::min(mask.h,surface->GetHeight()-y);
    if(x<0 || y<0 || width<=0 || height<=0) return;
    int pitch=surface->GetPitch();
    auto base=static_cast<BYTE*>(surface->Lock(x,y));
    if(!base) return;
    const int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    const int masks[]={255>>Drawing::RedShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    for(int j=0;j<height;++j) {
        auto row=reinterpret_cast<WORD*>(base+j*pitch);
        for(int i=0;i<width;++i) {
            int a=mask.alpha[j*mask.w+i]; if(!a) continue;
            int pixel=0;
            for(int k=0;k<3;++k) {
                int fg=(color>>shifts[k])&masks[k],bg=(row[i]>>shifts[k])&masks[k];
                pixel|=((fg*a+bg*(255-a)+127)/255)<<shifts[k];
            }
            row[i]=static_cast<WORD>(pixel);
        }
    }
    surface->Unlock();
}
}
