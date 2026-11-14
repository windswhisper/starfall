#include "Research.h"
#include "ConfigText.h"
#include <cstdio>
#include <cstdlib>

// Only the game boundary is substituted. The real model and text utilities are
// linked as separate translation units, with no game headers or process needed.
namespace {
unsigned seed=1234567, effectClears=0, presentationClears=0;
bool effectsSucceed=true, stoppedAudio=false;
void check(bool ok,const char* label) {
    if(!ok) { std::fprintf(stderr,"FAIL %s\n",label);std::exit(1); }
}
std::vector<std::string> ids(int house,const char* country="AMERICA") {
    std::vector<std::string> result;
    for(int index:Research::offers(house,country)) result.push_back(Research::options[index].id);
    return result;
}
void fixture() {
    Research::reset();
    Research::rarityWeights={5,4,1,0};
    Research::allowCardRefresh=true;
    for(int i=0;i<8;++i) {
        Research::Option option;
        option.id="common"+std::to_string(i);
        option.validChain=true;
        option.countries={"america"};
        Research::options.push_back(option);
    }
    Research::Option rare;
    rare.id="rare";rare.rarity=Research::Rarity::Rare;rare.validChain=true;rare.countries={"japan"};
    Research::options.push_back(rare);
    auto blocked=rare;blocked.id="blocked";blocked.validChain=false;blocked.countries={"america"};
    Research::options.push_back(blocked);
    auto prerequisite=rare;prerequisite.id="next";prerequisite.countries={"america"};prerequisite.prerequisites={"common0"};
    Research::options.push_back(prerequisite);
    auto invalid=rare;invalid.id="invalid";invalid.rarity=Research::Rarity::Invalid;
    Research::options.push_back(invalid);
}
}
void Research::ensureReady() {}
bool Research::available(int option) {
    return enabled && option>=0 && option<static_cast<int>(options.size()) && options[option].rarity!=Rarity::Invalid;
}
bool Research::executeEffect(int,int) { return effectsSucceed; }
void Research::clearPresentationCaches(bool stopAudio) { ++presentationClears;stoppedAudio=stopAudio; }
namespace Research::Detail {
void clearEffectCaches() { ++effectClears; }
unsigned matchSeed() { return seed; }
}

int main() {
    check(ConfigText::split(" A, b, A , ,C ")==std::vector<std::string>({"a","b","c"}),"normalized unique configuration list");
    check(ConfigText::number("100%",true)==100 && ConfigText::number("-1")==-1 &&
        ConfigText::number("2147483648")==-1 && ConfigText::number("1x")==-1,"strict configuration numbers");
    check(ConfigText::displayText("科研")==L"科研","UTF-8 display text");

    fixture();
    const auto first=ids(0);
    check(first.size()==3 && std::all_of(first.begin(),first.end(),[](const auto& id){return id.starts_with("common");}),"country, rarity and prerequisite filtering");
    check(ids(0,"america")==first,"country normalization");
    check(ids(1,"JAPAN")==std::vector<std::string>{"rare"},"country isolation");
    check(ids(2,"france").empty(),"empty eligible pool");
    Research::clearOfferCache();
    check(ids(0)==first,"saved offers survive cache invalidation");
    std::reverse(Research::options.begin(),Research::options.end());
    Research::clearOfferCache();
    check(ids(0)==first,"saved IDs survive registry reordering");
    fixture();
    check(ids(0)==first,"fresh state repeats deterministic offers");

    Research::award(0,300);
    const auto other=ids(1);
    int original=Research::offers(0,"america")[0];
    check(Research::canRefresh(0,"america",0),"refresh is available");
    check(!Research::refresh(0,"america",0,9999) && ids(0)==first,"stale command is inert");
    check(Research::refresh(0,"america",0,original),"refresh commits");
    const auto refreshed=ids(0);
    check(refreshed[0]!=first[0] && Research::seenOffers[0].contains(first[0]) &&
        Research::seenOffers[0].contains(refreshed[0]) && Research::slotUsed(0,0),"refresh records previous and new cards");
    check(!Research::refresh(0,"america",0,original) && ids(1)==other,"duplicate command and house isolation");
    Research::clearOfferCache();
    check(ids(0)==refreshed && !Research::canRefresh(0,"america",0),"refreshed board and slot survive cache rebuild");

    const int choice=Research::offers(0,"america")[0];
    const auto project=Research::options[choice].id;
    const auto beforeClear=effectClears;
    effectsSucceed=false;
    check(!Research::purchase(0,choice,"america") && Research::balance(0)==300 &&
        !Research::owns(0,project) && ids(0)==refreshed && effectClears==beforeClear,"failed effect preserves purchase state");
    effectsSucceed=true;
    check(Research::purchase(0,choice,"america") && Research::balance(0)==200 &&
        Research::owns(0,project) && Research::unlockOrder[0]==std::vector<std::string>{project},"purchase commits once");
    check(effectClears>beforeClear && !Research::savedOffers.contains(0) &&
        !Research::seenOffers.contains(0) && !Research::refreshedSlots.contains(0),"purchase invalidates dependent caches");
    check(!Research::purchase(0,choice,"america") && Research::balance(0)==200,"duplicate purchase rejected");
    const auto next=ids(0);
    check(std::find(next.begin(),next.end(),project)==next.end(),"owned project excluded from future offers");
    Research::award(0,INT_MAX);
    check(Research::balance(0)==INT_MAX,"point overflow saturates");
    Research::award(-1,100);
    check(!Research::points.contains(-1),"invalid owner rejected");
    Research::enabled=false;
    Research::award(1,100);
    check(Research::balance(1)==0 && Research::offers(0,"america").empty() &&
        !Research::purchase(0,choice,"america"),"disabled research blocks grants, offers and purchases");

    const auto generation=Research::uiGeneration;
    const auto cleared=effectClears, presented=presentationClears;
    Research::producers[1]={0,2};Research::buildingRewards[2]={0,0,true};
    Research::globalIncome.initialized=true;Research::expanded=true;
    Research::reset();
    check(Research::options.empty() && Research::unlocked.empty() && Research::unlockOrder.empty() &&
        Research::points.empty() && Research::savedOffers.empty() && Research::seenOffers.empty() &&
        Research::refreshedSlots.empty() && Research::producers.empty() && Research::buildingRewards.empty() &&
        !Research::globalIncome.initialized && !Research::expanded && Research::enabled,"scenario reset clears owned state");
    check(Research::uiGeneration==generation+1 && effectClears==cleared+1 &&
        presentationClears==presented+1 && stoppedAudio,"reset invalidates effect and presentation caches");
    std::puts("PASS independent research model: deterministic offers, reload, isolation, refresh, purchase, reset and configuration text");
}
