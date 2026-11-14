#include "../../../src/Starfall.cpp"
#include <WWMouseClass.h>
#include <WWKeyboardClass.h>
#include <VocClass.h>
#include "ResearchAudio.h"
#include <Drawing.h>

EXPORT_FUNC(SF_ResearchDraw);
#include "../WindowMouse.h"
namespace {
void verify(bool ok,const char* label) {
    std::ofstream("hud.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void input(Point2D point,int flags) {
    verify(WindowMouse::button(point,flags),"engine blocks click through");
}
void click(Point2D point) {input(point,1);input(point,4);}
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
    if(frame>=96) {
        Research::clearPresentationCaches(true);
        verify(ResearchAudio::voices().empty() && ResearchAudio::sampleCache().size()==0,"scenario reset releases voices and cached audio");
        TerminateProcess(GetCurrentProcess(),0);return 0;
    }
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(loading && frame<=45) return 0;
    if(frame!=20 && frame!=32 && frame!=45 && frame!=60 && frame!=88) return 0;
    // Single-player launches can supply different engine seeds. Pin this fixture
    // and clear any locally generated board before exercising all three refreshes.
    if(frame==20) {
        Game::Seed=static_cast<int>(3261594250u);
        Research::savedOffers.clear();Research::seenOffers.clear();Research::clearOfferCache();
    }
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
        verify(Research::options.size()==9,"registered projects");
        verify(Research::isEnabled(),"research enabled by default");
        Research::points[house]=100;
        const auto board=Research::offers(house,country);
        verify(!board.empty(),"funded board before disable");
        const auto savedBoard=Research::savedOffers.at(house);
        verify(WritePrivateProfileStringA("Starfall.Research","Enabled","no",configPath),"write disabled research");
        Research::readConfiguration(configPath);
        Research::award(house,50);
        verify(!Research::isEnabled() && Research::offers(house,country).empty() &&
            !Research::purchase(house,board[0],country) && !Research::refresh(house,country,0,board[0]) &&
            Research::balance(house)==100 && Research::savedOffers.at(house)==savedBoard,"disabled research blocks rewards and spending");
        Point2D border{x+24,y+1};
        const int marker=Drawing::RGB_To_Int(255,0,255);
        DSurface::Composite->SetPixel(&border,marker);
        SF_ResearchDraw(nullptr);
        verify(pixel(border.X,border.Y)==marker,"disabled HUD not drawn");
        verify(!WindowMouse::button(tab,1) && !WindowMouse::button(tab,4) && !Research::expanded,"disabled HUD passes input");
        verify(WritePrivateProfileStringA("Starfall.Research","Enabled",nullptr,configPath),"restore research default");
        Research::readConfiguration(configPath);
        verify(Research::isEnabled() && Research::offers(house,country)==board,"reenabled board retained");
        char originalPath[MAX_PATH]{};
        strcpy_s(originalPath,configPath);
        GetFullPathNameA("research-unregistered.ini",MAX_PATH,configPath,nullptr);
        {std::ofstream ini(configPath);ini<<"[Starfall.Research]\nOptions=SF_Repair\n";}
        Research::readConfiguration(configPath);
        verify(Research::options.empty(),"legacy options ignored");
        {std::ofstream ini(configPath);ini<<"[Starfall.Research]\n";}
        Research::readConfiguration(configPath);
        verify(Research::options.empty(),"unregistered projects empty");
        {
            std::ofstream ini(configPath);
            ini<<"[Starfall.Research]\nAllowCardRefresh=yes\n[Starfall.ResearchTypes]\n";
            for(int i=0;i<4;++i) ini<<i<<"=Red"<<i<<"\n";
            for(int i=0;i<4;++i) ini<<"[Red"<<i<<"]\nRarity=Insane\nEffect=GiveCredits\nCredits=10\n";
        }
        Research::readConfiguration(configPath);Research::clearOfferCache();
        verify(Research::options.size()==4 && Research::options[0].rarity==Research::Rarity::Insane &&
            Research::offers(house,country).empty(),"insane disabled by default");
        WritePrivateProfileStringA("Starfall.Research","RarityWeight.Insane","1",configPath);
        Research::readConfiguration(configPath);Research::clearOfferCache();
        const auto red=Research::offers(house,country);
        verify(red.size()==3 && Research::refresh(house,country,0,red.front()) &&
            Research::offers(house,country).front()!=red.front(),"insane draw and refresh");
        WritePrivateProfileStringA("Starfall.Research","RarityWeight.Insane","0",configPath);
        Research::readConfiguration(configPath);Research::clearOfferCache();
        verify(Research::offers(house,country).empty(),"zero excludes saved insane offers");
        Research::refreshedSlots.erase(house);
        Research::savedOffers[house]=savedBoard;
        Research::clearOfferCache();
        strcpy_s(configPath,originalPath);
        Research::readConfiguration(configPath);
        verify(Research::options.size()==9,"registered projects restored");
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
        auto playing=ResearchAudio::voices().size();
        auto voice=playing?ResearchAudio::voices().back().get():nullptr;
        auto data=voice?voice->header.lpData:nullptr;
        for(int i=0;i<70;++i) ResearchAudio::sampleCache().get("eviction-"+std::to_string(i),[] {
            ResearchAudio::Sample sample;sample.data.resize(32768);return sample;
        },[](const auto& key,const auto& sample){return sample.data.capacity()+2*(key.size()+1)+sizeof(sample);});
        verify(!ResearchAudio::sampleCache().contains("ui-test.wav"),"playing sound evicted from sample cache");
        Research::readConfiguration(configPath);
        verify(ResearchAudio::sampleCache().size()==0 && ResearchAudio::sampleCache().bytes()==0 &&
               ResearchAudio::voices().size()==playing && (!voice || (voice->header.lpData==data &&
               data==reinterpret_cast<LPSTR>(voice->data.data()) && voice->device)),"configuration reload preserves playing audio and clears cache");
        Research::points[house]=0;
        SF_ResearchDraw(nullptr);
        verify(pixel(x+24,y+1)==track && pixel(x+24,y+46)==track && pixel(x+1,y+24)==track,"empty border");
        input(tab,1);input({tab.X-100,tab.Y+100},4);
        input(tab,16);input(tab,64);
        click(tab);verify(!Research::expanded,"disabled at zero");
        Research::award(house,99);
        click(tab);verify(!Research::expanded,"disabled at 99");
        input(tab,1);Research::award(house,1);input(tab,4);
        verify(!Research::expanded,"funded mid-click");
        input(tab,1);
        reinterpret_cast<WNDPROC>(0x7775C0)(Game::hWnd,WM_CANCELMODE,0,0);
        input({view.X+3,view.Y+view.Height+3},4);
        verify(!Research::expanded,"canceled HUD release cannot issue an order");
        verify(WindowMouse::send(tab,WM_LBUTTONDBLCLK) && WindowMouse::button(tab,4) &&
            Research::expanded,"opened at 100");
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
        Research::readConfiguration(configPath); Research::clearOfferCache();
        auto disabledBoard=Research::offers(house,country);
        verify(!Research::allowCardRefresh && !Research::refresh(house,country,0,disabledBoard.front()),"ini disables refresh");
        verify(WritePrivateProfileStringA("Starfall.Research","AllowCardRefresh","yes",configPath),"restore fixture");
        Research::readConfiguration(configPath); Research::clearOfferCache();
        initial=Research::offers(house,country);
        verify(initial.size()==3,"three cards");
        WWMouseClass::Instance->XY1={cx,view.Y+4};
        SF_ResearchDraw(nullptr);
        Sleep(160);SF_ResearchDraw(nullptr);
        captureHud("research-opening.ppm",cx-(3*cw+2*gap)/2-18,view.Y+(view.Height-ch-72)/2-10,3*cw+2*gap+36,ch+92);
        Sleep(360);
        SF_ResearchDraw(nullptr);
        captureHud("research-cards.ppm",cx-(3*cw+2*gap)/2-18,view.Y+(view.Height-ch-72)/2-10,3*cw+2*gap+36,ch+92);
        input({cx,cy},1);input({view.X+3,view.Y+view.Height-3},4);
        verify(Research::expanded && Research::unlocked[house].empty(),"card drag cannot select behind");
        input({cx-cw-gap,refreshY},1);input({cx-cw-gap,refreshY},4);
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
        {
            std::ofstream diagnostic("hud.log",std::ios::app);
            diagnostic<<"INFO offers seed="<<static_cast<unsigned>(Game::Seed)<<" house="<<house<<" initial=";
            for(int index:initial) diagnostic<<Research::options[index].id<<',';
            diagnostic<<" refreshed=";
            for(int index:rows) diagnostic<<Research::options[index].id<<',';
            diagnostic<<'\n';
        }
        const std::set<string> eligible={"repair","drain","riposte","reserve","focus","extra"};
        verify(std::set<int>(rows.begin(),rows.end()).size()==3 && std::all_of(rows.begin(),rows.end(),[&](int i){
            return i>=0 && i<static_cast<int>(Research::options.size()) && eligible.contains(Research::options[i].id);
        }),"unique eligible projects");
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
        input({cx,cy},1);input({cx,cy},4);
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
        input({left+38,top+20},1);input({left+38,top+20},4);
        WWMouseClass::Instance->XY1=tab;
        SF_ResearchDraw(nullptr);
        auto cur=WWMouseClass::Instance->XY1;
        std::ofstream("hud.log",std::ios::app)<<"pointer "<<cur.X<<","<<cur.Y<<" tab "<<x<<","<<y<<" sample "<<pixel(x+42,y+59)<<" expected "<<Drawing::RGB_To_Int(14,23,36)<<"\n";
        captureHud("hud-half.ppm",view.X+view.Width-276,y-2,276,128);
        verify(pixel(x+42,y+59)==Drawing::RGB_To_Int(14,23,36),"balance tooltip");
        Research::award(house,50);
        verify(Research::offers(house,country).size()==3 && !Research::canRefresh(house,country,0) &&
            !Research::slotUsed(house,0),"no alternative preserves allowance");
    }
    WWMouseClass::Instance->SetCoords(previous);
    return 0;
}
