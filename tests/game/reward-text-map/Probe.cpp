#include "../../../src/Starfall.cpp"
#include "../../../src/ResearchText.h"

namespace {
bool hasTankLabel() {
    auto viewer=HouseClass::CurrentPlayer;
    auto tactical=TacticalClass::Instance;
    auto surface=DSurface::Composite;
    for(auto unit:UnitClass::Array) {
        if(unit->Owner!=viewer || _stricmp(unit->Type->ID,"MTNK")) continue;
        auto coords=unit->GetCoords();
        GScreenClass::Instance.Render();
        const auto& mask=ResearchText::maskFor(L"灰熊坦克",13);
        auto point=tactical->CoordsToClient(coords).first;
        const int x=point.X-(mask.w+8)/2;
        const int y=point.Y-32+unit->Type->PixelSelectionBracketDelta-mask.h-4;
        if(mask.alpha.empty() || x<0 || y<0 || x+mask.w+8>surface->Width || y+mask.h+4>surface->Height) return false;
        const int pitch=surface->GetPitch();
        auto pixels=static_cast<const BYTE*>(surface->Lock(x,y));
        if(!pixels) return false;
        int matches=0,ink=0;
        auto background=Drawing::RGB_To_Int(14,27,43);
        for(int j=0;j<mask.h;++j) {
            auto row=reinterpret_cast<const WORD*>(pixels+(j+2)*pitch)+4;
            for(int i=0;i<mask.w;++i) {
                if(mask.alpha[j*mask.w+i]>200) {++ink;matches+=row[i]!=background;}
                else if(mask.alpha[j*mask.w+i]==0) matches+=row[i]==background;
            }
        }
        surface->Unlock();
        return ink>20 && matches==std::count_if(mask.alpha.begin(),mask.alpha.end(),[](BYTE a){return !a || a>200;});
    }
    return false;
}
}

DEFINE_HOOK(0x55B4EB,SF_TestRewardTextMap,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int frame=Unsorted::CurrentFrame;
    auto house=HouseClass::CurrentPlayer;
    static int credits,points;
    if(frame==20) {
        for(auto unit:UnitClass::Array) if(unit->Owner==house && !_stricmp(unit->Type->ID,"MTNK")) {
            auto coords=unit->GetCoords(); TacticalClass::Instance->SetTacticalPosition(&coords); break;
        }
        credits=house->Available_Money();
        points=Research::balance(house->ArrayIndex);
    }
    auto verify=[&](bool ok,const char* what) {
        std::ofstream("reward-text-map.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<what<<"\n";
        if(!ok) TerminateProcess(GetCurrentProcess(),2);
    };
    if(frame==30) verify(hasTankLabel(),"configured Chinese label rendered above the tank");
    if(frame==70 || frame==145 || frame==220 || frame==295) {
        const auto& labels=RewardText::labels;
        const auto cash=house->Available_Money()-credits;
        const auto rp=Research::balance(house->ArrayIndex)-points;
        if(frame==70)
            verify(cash==50 && rp==0 && labels.size()==1 && labels.back().text==L"+$50","cash phase");
        if(frame==145)
            verify(cash==50 && rp==50 && labels.size()==1 && labels.back().text==L"+ 50 RP","research phase");
        if(frame==220)
            verify(cash==60 && rp==60 && labels.empty(),"silent phase");
        if(frame==295) {
            verify(cash==110 && rp==60 && labels.size()==1 && labels.back().text==L"+$50","cycle repeats");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    return 0;
}
