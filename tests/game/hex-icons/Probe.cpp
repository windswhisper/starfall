#include "../../../src/ResearchUI.cpp"
#include <fstream>
#include <Unsorted.h>

namespace {
void check(bool ok,const char* label) {
    if(ok) return;
    std::ofstream("hex-icons.log",std::ios::app)<<"FAIL "<<label<<"\n";
    TerminateProcess(GetCurrentProcess(),2);
}
std::vector<WORD> snapshot(RectangleStruct rect) {
    auto surface=DSurface::Composite;
    int pitch=surface->GetPitch();
    auto base=static_cast<const BYTE*>(surface->Lock(rect.X,rect.Y));
    check(base!=nullptr,"surface lock");
    std::vector<WORD> result(rect.Width*rect.Height);
    for(int y=0;y<rect.Height;++y) memcpy(result.data()+y*rect.Width,base+y*pitch,rect.Width*2);
    surface->Unlock();
    return result;
}
void capture(RectangleStruct rect) {
    std::ofstream f("hex-icons.ppm",std::ios::binary);
    f<<"P6\n"<<rect.Width<<" "<<rect.Height<<"\n255\n";
    int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    int masks[]={255>>Drawing::RedShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    for(auto value:snapshot(rect)) for(int k=0;k<3;++k)
        f.put(static_cast<char>(((value>>shifts[k])&masks[k])*255/masks[k]));
}
}
DEFINE_HOOK(0x55B4EB,SF_TestHexIcons,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES || Unsorted::CurrentFrame!=20) return 0;
    Research::ensureReady();
    auto& projects=Research::options;
    check(projects.size()>=15,"project count");
    auto surface=DSurface::Composite;
    RectangleStruct sheet{8,8,480,288};
    int background=color(14,23,35);
    surface->FillRect(&sheet,background);
    std::set<std::string> unique;
    int rareCount=0;
    for(int i=0;i<15;++i) {
        const auto& o=projects[i];
        RectangleStruct r{sheet.X+(i%5)*96,sheet.Y+(i/5)*96,96,96};
        check(unique.insert(o.icon).second,"unique file");
        PngImage raw;
        check(loadPng(o.icon,0,raw) && raw.width==96 && raw.height==96,"registered image");
        drawPng(raw,r,0);
        auto original=snapshot(r);
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto actual=snapshot(r);
        bool rare=o.rarity==Research::Rarity::Rare;
        check((original!=actual)==(o.rarity!=Research::Rarity::Common) && actual.front()==background,"rarity and transparent corner");
        if(rare) {
            ++rareCount;
            size_t blue=0;
            for(auto value:actual) {
                if(value==background) continue;
                int b=((value>>Drawing::BlueShiftLeft)&(255>>Drawing::BlueShiftRight))<<Drawing::BlueShiftRight;
                int g=((value>>Drawing::GreenShiftLeft)&(255>>Drawing::GreenShiftRight))<<Drawing::GreenShiftRight;
                if(b>g+24) ++blue;
            }
            check(blue>700,"blue-violet coverage");
        }
        Research::Option epic=o;
        epic.rarity=Research::Rarity::Epic;
        surface->FillRect(&r,background);
        drawProjectIcon(epic,r);
        auto gold=snapshot(r);
        size_t goldPixels=0;
        for(auto value:gold) {
            if(value==background) continue;
            int red=((value>>Drawing::RedShiftLeft)&(255>>Drawing::RedShiftRight))<<Drawing::RedShiftRight;
            int green=((value>>Drawing::GreenShiftLeft)&(255>>Drawing::GreenShiftRight))<<Drawing::GreenShiftRight;
            int blue=((value>>Drawing::BlueShiftLeft)&(255>>Drawing::BlueShiftRight))<<Drawing::BlueShiftRight;
            if(red>blue+40 && green>blue+20) ++goldPixels;
        }
        check(gold!=original && gold.front()==background && goldPixels>700,"gold coverage and transparent corner");
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
    }
    check(rareCount==5,"rare count");
    capture(sheet);
    std::ofstream("hex-icons.log",std::ios::app)<<"PASS atlas and rarity\n";
    RectangleStruct r{8,8,30,30};
    for(const char* name:{"hx-test.pcx","missing-hx.png"}) {
        Research::Option o;
        o.icon=name;
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto original=snapshot(r);
        o.rarity=Research::Rarity::Rare;
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto tinted=snapshot(r);
        check(original!=tinted && tinted[15*30+15]==background && tinted[4*30+15]!=background,"small transparent PCX or fallback");
        o.rarity=Research::Rarity::Epic;
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto gold=snapshot(r);
        check(gold!=original && gold!=tinted && gold[15*30+15]==background && gold[4*30+15]!=background,"gold transparent PCX or fallback");
    }
    std::ofstream("hex-icons.log",std::ios::app)<<"PASS PCX and fallback\n";
    TerminateProcess(GetCurrentProcess(),0);
    return 0;
}
