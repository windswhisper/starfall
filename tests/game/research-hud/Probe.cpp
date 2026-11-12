#include "../../../src/Starfall.cpp"
#include <WWMouseClass.h>
#include <WWKeyboardClass.h>
#include <VocClass.h>
#include "ResearchAudio.h"
#include <Drawing.h>

EXPORT_FUNC(SF_ResearchDraw);
EXPORT_FUNC(SF_ResearchInput);
namespace {
void verify(bool ok,const char* label) {
    std::ofstream("hud.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void input(Point2D point,int flags) {
    REGISTERS regs{};
    DWORD stack[20]{},key=123;
    regs.ESP(stack);regs.Stack(0x1c,point);regs.Stack(0x34,flags);regs.Stack(0x38,&key);
    verify(SF_ResearchInput(&regs)==0x4AAD19 && key==0,"consumed input");
}
void click(Point2D point) {input(point,1);input(point,4);}
void engineInput(Point2D point,int flags) {
    auto keyboard=WWKeyboardClass::Instance;
    int x=keyboard->MouseQX,y=keyboard->MouseQY;
    keyboard->MouseQX=point.X;keyboard->MouseQY=point.Y;
    DWORD key=123;
    using ActionFn=int(__thiscall*)(void*,int,DWORD*,int);
    auto action=reinterpret_cast<ActionFn>(0x4AAC10);
    int result=action(reinterpret_cast<void*>(0x87F7E8),flags,&key,0);
    keyboard->MouseQX=x;keyboard->MouseQY=y;
    verify(result==1 && key==0,"engine blocks click through");
}
int pixel(int x,int y) {
    Point2D p{x,y};
    return DSurface::Composite->GetPixel(&p);
}
void captureHud(const char* name,int x0,int top,int width,int height) {
    auto surface=DSurface::Composite;
    const int pitch=surface->GetPitch();
    auto base=static_cast<BYTE*>(surface->Lock(x0,top));
    if(!base) return;
    std::ofstream file(name,std::ios::binary);
    file<<"P6\n"<<width<<" "<<height<<"\n255\n";
    for(int y=0;y<height;++y) {
        auto row=reinterpret_cast<const WORD*>(base+y*pitch);
        for(int x=0;x<width;++x) {
            WORD value=row[x];
            for(auto shift:std::array<std::pair<int,int>,3>{{{Drawing::RedShiftLeft,Drawing::RedShiftRight},
                {Drawing::GreenShiftLeft,Drawing::GreenShiftRight},{Drawing::BlueShiftLeft,Drawing::BlueShiftRight}}}) {
                int mask=255>>shift.second;
                file.put(static_cast<char>(((value>>shift.first)&mask)*255/mask));
            }
        }
    }
    surface->Unlock();
}
}
DEFINE_HOOK(0x55B4EB,SF_TestResearchHUD,5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    int frame=Unsorted::CurrentFrame;
    if(frame>=96) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(loading && frame<=45) return 0;
    if(frame!=20 && frame!=32 && frame!=45 && frame!=60 && frame!=88) return 0;
    Research::ensureReady();
    int house=HouseClass::CurrentPlayer->ArrayIndex;
    auto view=DSurface::ViewBounds;
    int x=view.X+view.Width-56,y=view.Y+24;
    Point2D tab{x+24,y+24};
    auto previous=WWMouseClass::Instance->XY1;
    WWMouseClass::Instance->SetCoords(tab);
    WWMouseClass::Instance->XY1=tab;
    int track=Drawing::RGB_To_Int(42,64,79),lit=Drawing::RGB_To_Int(60,218,251);
    int cx=view.X+view.Width/2,cy=view.Y+view.Height/2;
    int gap=view.Width>=720?16:8;
    int cw=std::min({244,(view.Width-24-2*gap)/3,(view.Height-94)*2/3});
    int ch=cw*3/2;
    int refreshY=view.Y+(view.Height-ch-72)/2+38+ch+20;
    const char* country=HouseClass::CurrentPlayer->Type->ID;
    static vector<int> initial;
    if(frame==20) {
        verify(Research::options.size()==8,"registered projects");
        verify(Research::isEnabled(),"research enabled by default");
        Research::points[house]=100;
        const auto board=Research::offers(house,country);
        verify(!board.empty(),"funded board before disable");
        const auto savedBoard=Research::savedOffers.at(house);
        verify(WritePrivateProfileStringA("Starfall.Research","Enabled","no",configPath),"write disabled research");
        readResearch();
        Research::award(house,50);
        verify(!Research::isEnabled() && Research::offers(house,country).empty() &&
            !Research::purchase(house,board[0],country) && !Research::refresh(house,country,0,board[0]) &&
            Research::balance(house)==100 && Research::savedOffers.at(house)==savedBoard,"disabled research blocks rewards and spending");
        Point2D border{x+24,y+1};
        const int marker=Drawing::RGB_To_Int(255,0,255);
        DSurface::Composite->SetPixel(&border,marker);
        SF_ResearchDraw(nullptr);
        verify(pixel(border.X,border.Y)==marker,"disabled HUD not drawn");
        REGISTERS regs{};DWORD stack[20]{},key=123;
        regs.ESP(stack);regs.Stack(0x1c,tab);regs.Stack(0x34,1);regs.Stack(0x38,&key);
        verify(SF_ResearchInput(&regs)==0 && key==123 && !Research::expanded,"disabled HUD passes input");
        verify(WritePrivateProfileStringA("Starfall.Research","Enabled",nullptr,configPath),"restore research default");
        readResearch();
        verify(Research::isEnabled() && Research::offers(house,country)==board,"reenabled board retained");
        char originalPath[MAX_PATH]{};
        strcpy_s(originalPath,configPath);
        GetFullPathNameA("research-unregistered.ini",MAX_PATH,configPath,nullptr);
        {std::ofstream ini(configPath);ini<<"[Starfall.Research]\nOptions=SF_Repair\n";}
        readResearch();
        verify(Research::options.empty(),"legacy options ignored");
        {std::ofstream ini(configPath);ini<<"[Starfall.Research]\n";}
        readResearch();
        verify(Research::options.empty(),"unregistered projects empty");
        strcpy_s(configPath,originalPath);
        readResearch();
        verify(Research::options.size()==8,"registered projects restored");
        verify(RulesClass::Instance->GenericClick>=0 && RulesClass::Instance->GenericClick<VocClass::Array.Count,"native click sound configured");
        verify(std::all_of(Research::soundFiles.begin(),Research::soundFiles.end(),[](const auto& p){return p=="ui-test.wav";}) && !ResearchAudio::load("ui-test.wav").data.empty(),"configured wav loaded");
        verify(!ResearchAudio::load("builtin:cards").data.empty() && !ResearchAudio::load("builtin:select").data.empty() &&
            !ResearchAudio::load("builtin:available").data.empty(),"embedded wav loaded");
        auto configured=Research::soundFiles[0];Research::soundFiles[0]="builtin:cards";
        bool builtinPlayed=ResearchAudio::play(Research::SoundCue::Button);
        Research::soundFiles[0]=configured;
        verify(builtinPlayed,"embedded wav playback");
        auto oldVoices=ResearchAudio::voices().size();
        bool wavPlayed=ResearchAudio::play(Research::SoundCue::Button);
        verify(wavPlayed && (GameOptionsClass::Instance.SoundVolume<=0 || ResearchAudio::voices().size()>oldVoices),"wav playback queued");
        Research::points[house]=0;
        SF_ResearchDraw(nullptr);
        verify(pixel(x+24,y+1)==track && pixel(x+24,y+46)==track && pixel(x+1,y+24)==track,"empty border");
        engineInput(tab,1);engineInput({tab.X-100,tab.Y+100},4);
        engineInput(tab,16);engineInput(tab,64);
        click(tab);verify(!Research::expanded,"disabled at zero");
        Research::award(house,99);
        click(tab);verify(!Research::expanded,"disabled at 99");
        input(tab,1);Research::award(house,1);input(tab,4);
        verify(!Research::expanded,"funded mid-click");
        click(tab);verify(Research::expanded,"opened at 100");
        SF_ResearchDraw(nullptr);
        int top=pixel(x+24,y+1);
        verify(top!=track && top==pixel(x+1,y+24) && top==pixel(x+24,y+46),"full border");
        Research::points[house]=99;
        SF_ResearchDraw(nullptr);
        verify(!Research::expanded,"closed after balance changed");
        SF_ResearchDraw(nullptr);
        Research::award(house,51);
        click(tab);
        verify(WritePrivateProfileStringA("Starfall.Research","AllowCardRefresh","no",configPath),"write disabled fixture");
        readResearch(); Research::clearOfferCache();
        auto disabledBoard=Research::offers(house,country);
        verify(!Research::allowCardRefresh && !Research::refresh(house,country,0,disabledBoard.front()),"ini disables refresh");
        verify(WritePrivateProfileStringA("Starfall.Research","AllowCardRefresh","yes",configPath),"restore fixture");
        readResearch(); Research::clearOfferCache();
        initial=Research::offers(house,country);
        verify(initial.size()==3,"three cards");
        WWMouseClass::Instance->XY1={cx,view.Y+4};
        SF_ResearchDraw(nullptr);
        Sleep(160);SF_ResearchDraw(nullptr);
        captureHud("research-opening.ppm",cx-(3*cw+2*gap)/2-18,view.Y+(view.Height-ch-72)/2-10,3*cw+2*gap+36,ch+92);
        Sleep(360);
        SF_ResearchDraw(nullptr);
        captureHud("research-cards.ppm",cx-(3*cw+2*gap)/2-18,view.Y+(view.Height-ch-72)/2-10,3*cw+2*gap+36,ch+92);
        engineInput({cx,cy},1);engineInput({view.X+3,view.Y+view.Height-3},4);
        verify(Research::expanded && Research::unlocked[house].empty(),"card drag cannot select behind");
        engineInput({cx-cw-gap,refreshY},1);engineInput({cx-cw-gap,refreshY},4);
    }
    if(frame==32) {
        auto rows=Research::offers(house,country);
        verify(rows[0]!=initial[0] && rows[1]==initial[1] && rows[2]==initial[2] &&
            Research::slotUsed(house,0) && !Research::slotUsed(house,1) && !Research::slotUsed(house,2),"independent refresh");
        click({cx-cw-gap,refreshY});
        click({cx,refreshY});
        click({cx+cw+gap,refreshY});
    }
    if(frame==45) {
        verify(Research::refreshedSlots.at(house)==7 && Research::balance(house)==150,"three free refreshes");
        auto rows=Research::offers(house,country);
        verify(std::set<int>(rows.begin(),rows.end()).size()==3 && std::all_of(rows.begin(),rows.end(),[](int i){return i<5;}),"unique eligible projects");
        {std::ofstream output("expected-cards.txt");for(int index:rows) output<<Research::options[index].id<<"\n";}
        click(tab); click(tab);
        verify(Research::offers(house,country)==rows && !Research::canRefresh(house,country,0),"reopen retains cards");
        verify(ScenarioClass::SaveGame("Saved Games\\ResearchHUD.sav",L"ResearchHUD"),"saved");
    }
    if(frame==60) {
        auto rows=Research::offers(house,country);
        std::ifstream expected("expected-cards.txt");
        for(int index:rows) {string id;expected>>id;verify(id==Research::options[index].id,"retained option");}
        verify(rows.size()==3 && Research::refreshedSlots.at(house)==7 && !Research::canRefresh(house,country,1),"retained refresh limits");
        if(!Research::expanded) click(tab);
        engineInput({cx,cy},1);engineInput({cx,cy},4);
        verify(Research::expanded,"purchase queued");
    }
    if(frame==88) {
        verify(Research::balance(house)==50 && Research::unlocked[house].size()==1 && !Research::refreshedSlots.contains(house),"purchase");
        Sleep(750);SF_ResearchDraw(nullptr);
        verify(!Research::expanded,"purchase animation closes panel");
        Research::award(house,200);
        for(int i=0;i<2;++i) {
            auto option=Research::offers(house,country).front();
            verify(Research::purchase(house,option,country),"prior research");
        }
        verify(Research::balance(house)==50,"retained points");
        SF_ResearchDraw(nullptr);
        verify(pixel(x+1,y+23)==track && pixel(x+1,y+24)==lit &&
            pixel(x+46,y+23)==track && pixel(x+46,y+24)==lit &&
            pixel(x+24,y+1)==track && pixel(x+24,y+46)==lit,"half border");
        click(tab);verify(!Research::expanded,"disabled at 50");
        verify(Research::unlockOrder[house].size()==3,"completed icons");
        auto& firstId=Research::unlockOrder[house].front();
        auto first=std::find_if(Research::options.begin(),Research::options.end(),
            [&](const auto& o){return o.id==firstId;});
        verify(first!=Research::options.end(),"first badge");
        first->icon.clear(); // Exercise the default rather than a randomly chosen project icon.
        int left=x-4-40,top=y+4;
        WWMouseClass::Instance->XY1=tab;
        SF_ResearchDraw(nullptr);
        int plate=Drawing::RGB_To_Int(14,23,35);
        int transparent=0,colored=0;
        for(int py=2;py<38;++py) for(int px=2;px<38;++px)
            if(pixel(left+px,top+py)==plate) ++transparent;else ++colored;
        verify(pixel(left+1,top+1)==plate && transparent>40 && colored>40,"transparent badge on dark plate");
        captureHud("completed-badges.ppm",left-92,top-4,136,48);
        Point2D badge{left+20,top+20};
        WWMouseClass::Instance->XY1=badge;
        SF_ResearchDraw(nullptr);
        verify(pixel(left,top)==Drawing::RGB_To_Int(182,159,255),"left badge hover");
        engineInput({left+38,top+20},1);engineInput({left+38,top+20},4);
        WWMouseClass::Instance->XY1=tab;
        SF_ResearchDraw(nullptr);
        auto cur=WWMouseClass::Instance->XY1;
        std::ofstream("hud.log",std::ios::app)<<"pointer "<<cur.X<<","<<cur.Y<<" tab "<<x<<","<<y<<" sample "<<pixel(x+42,y+59)<<" expected "<<Drawing::RGB_To_Int(14,23,36)<<"\n";
        captureHud("hud-half.ppm",view.X+view.Width-276,y-2,276,128);
        verify(pixel(x+42,y+59)==Drawing::RGB_To_Int(14,23,36),"balance tooltip");
        Research::award(house,50);
        verify(Research::offers(house,country).size()==2 && !Research::canRefresh(house,country,0) &&
            !Research::slotUsed(house,0),"no alternative preserves allowance");
    }
    WWMouseClass::Instance->SetCoords(previous);
    return 0;
}
