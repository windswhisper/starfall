#include "ResearchInternal.h"
#include "ConfigText.h"
#include <cstdio>
#include <cstdlib>

// Only the game boundary is substituted. The real model and text utilities are
// linked as separate translation units, with no game headers or process needed.
namespace {
unsigned seed=1234567, effectClears=0, presentationClears=0;
bool effectsSucceed=true, stoppedAudio=false;
std::set<std::string> blockedEffects;
int randomPick=0,randomDraws=0,lastPoolSize=0;
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
void buildingWeightsTest() {
    using namespace Research;
    fixture();
    const std::vector<std::string> pool={"common0","common1","common2","common3","rare","invalid"};
    rarityWeights={1,3,0,0};
    int firstRare=0,mixed=0;
    const int drawsBefore=randomDraws;
    for(unsigned id=1;id<=512;++id) {
        const auto& board=Detail::buildingOfferRows(id,0,pool);
        check(board.size()==3 && std::set<int>(board.begin(),board.end()).size()==3,"building cards are distinct");
        firstRare+=options[board.front()].rarity==Rarity::Rare;
        mixed+=std::any_of(board.begin(),board.end(),[](int i){return options[i].rarity==Rarity::Rare;});
    }
    check(firstRare>335 && firstRare<435 && mixed>firstRare && randomDraws==drawsBefore,
        "building weights apply per tier and slot, independent of project count and game RNG");
    const auto board=Detail::buildingOfferRows(12,0,pool);
    clearOfferCache();
    check(Detail::buildingOfferRows(12,0,pool)==board,"building draw survives cache eviction");
    rarityWeights={0,1,0,0};clearOfferCache();
    check(Detail::buildingOfferRows(12,0,pool)==std::vector<int>{8},"only positive tiers offered when pool runs short");
    unlocked[0].insert("rare");clearOfferCache();
    check(Detail::buildingOfferRows(12,0,pool).empty(),"exhausted weighted building has no choices");
}
void repeatResearchTests() {
    using namespace Research;
    fixture();options.resize(1);
    options[0].repeatable=true;
    award(0,200);
    check(purchase(0,0,"america") && purchase(0,0,"america") && completedRounds[0]==2 && balance(0)==0 &&
        acquisitionCount(0,"common0")==2 && acquisitionCount(1,"common0")==0,"each repeat charges and advances only the purchaser");
    check(completedProjects(0)==std::vector<int>{0} && unlockOrder[0].size()==1 &&
        !canRefresh(0,"america",0),"repeat purchases share one owned project");
    check(grantNamed(0,"america","common0")==0 && grantRandom(0,"america")==0 &&
        grantFree(0,"america",{"common0"},{}) && ticketOptions(lastTicket,0,"america")==std::vector<int>{0} &&
        Detail::buildingOfferRows(1,0,{"common0"})==std::vector<int>{0} && acquisitionCount(0,"common0")==4,
        "owned repeatable project is eligible through named random free and building paths");
    check(grantNamed(0,"japan","common0")==-1 && acquisitionCount(0,"common0")==4,"repeats retain country requirements");
    options[0].repeatable=false;clearOfferCache();
    check(offers(0,"america").empty() && grantNamed(0,"america","common0")==-1 && acquisitionCount(0,"common0")==4,
        "disabling repeats retains earned copies and prevents further acquisition");
    fixture();options.resize(2);
    for(auto& o:options) {
        o.repeatable=true;o.effectList=true;
        EffectSpec cash;cash.effect=Effect::Credits;cash.credits=1;
        EffectSpec random;random.effect=Effect::RandomResearch;
        o.effects={cash,random};
    }
    check(grantNamed(0,"america","common0")==0 && acquisitionCount(0,"common0")==1 && acquisitionCount(0,"common1")==1 &&
        grantNamed(0,"america","common0")==0 && acquisitionCount(0,"common1")==2,"repeatable random chain stops and a later independent grant runs again");
}
void replacementTests() {
    using namespace Research;
    fixture();
    options.resize(4);
    options[2].replaces=options[2].prerequisites={"common0","common1"};
    options[3].replaces=options[3].prerequisites={"common2"};
    const std::vector<std::string> pool={"common2"};
    award(0,400);
    check(purchase(0,0,"america") && Detail::buildingOfferRows(1,0,pool).empty(),
        "every base is required even for building upgrades");
    check(purchase(0,1,"america"),"purchase second base");
    effectsSucceed=false;
    check(!purchase(0,2,"america") && owns(0,"common0") && owns(0,"common1") &&
        balance(0)==200 && retired.empty(),"failed upgrade preserves both bases and points");
    effectsSucceed=true;
    check(purchase(0,2,"america") && grantRandom(0,"america")==3 && completedProjects(0)==std::vector<int>{3},
        "ordinary and random upgrades retire complete chains");
    check(offers(0,"america").empty() && !canOfferFree(0,"america",{"common0"},{},-1) &&
        !offers(1,"america").empty() && balance(0)==100 && completedRounds[0]==3,
        "old tiers stay out of rewards for the upgraded house only");
}
void mixedPoolTests() {
    using namespace Research;
    fixture();
    mixedRarityPool=true;
    sharedRaritySequence=true;
    options.resize(6);
    options[4].rarity=Rarity::Rare;
    options[5].rarity=Rarity::Insane;
    rarityWeights={1,3,0,0};
    const std::vector<std::string> scope={"common0","common1","common2","common3","common4","common5"};
    std::array<int,3> firstRare{};
    const unsigned originalSeed=seed;
    const int drawsBefore=randomDraws;
    for(unsigned run=1;run<=512;++run) {
        seed=run;
        savedOffers.clear();seenOffers.clear();clearOfferCache();
        check(grantFree(0,"america",scope,{}),"create mixed research ticket");
        auto sample=[&](int kind,const std::vector<int>& board) {
            check(board.size()==3 && std::set<int>(board.begin(),board.end()).size()==3 &&
                std::all_of(board.begin(),board.end(),[](int i){return i>=0 && i<5;}),"distinct mixed cards exclude zero weight");
            firstRare[kind]+=board.front()==4;
        };
        sample(0,offers(0,"america"));
        sample(1,Detail::buildingOfferRows(run,0,scope));
        sample(2,ticketOptions(lastTicket,0,"america"));
    }
    for(int count:firstRare) check(count>175 && count<270,"each project contributes its tier weight (rare probability 3/7)");
    award(0,100);
    auto board=offers(0,"america");
    auto seen=seenOffers.at(0);
    check(refresh(0,"america",0,board.front()) && !seen.contains(savedOffers.at(0).front()),"mixed refresh picks unseen project");
    board=offers(0,"america");
    raritySequenceSeed=seed;
    seed=originalSeed;
    clearOfferCache();
    check(offers(0,"america")==board && slotUsed(0,0) && randomDraws==drawsBefore,"saved mixed board retains order and refresh state");
    rarityWeights={INT_MAX,INT_MAX,0,0};clearOfferCache();
    check(offers(0,"america").size()==3,"mixed weight totals can exceed 32 bits");
    rarityWeights={0,0,0,0};clearOfferCache();
    check(offers(0,"america").empty() && !canRefresh(0,"america",1),"empty mixed pool");
    check(grantFree(0,"america",scope,Rarity::Insane) && ticketOptions(lastTicket,0,"america")==std::vector<int>{5},
        "explicit free rarity retains zero-weight exception");
}
void freeChoiceTests() {
    using namespace Research;
    fixture();
    options[8].countries={"america"};
    rarityWeights={0,1,0,0};
    const std::vector<std::string> scope={"common4","common6","common0","rare"};
    const auto draws=randomDraws;
    check(grantFree(0,"AMERICA",scope,{}) && ticketOptions(lastTicket,0,"america")==std::vector<int>{8},
        "long free scope uses global rarity weights and never offers outside IDs");
    check(ticketOptions(lastTicket,1,"america").empty() && randomDraws==draws,"ticket belongs to one house and uses no game RNG");
    check(!grantFree(0,"america",{"unknown","blocked"},{}) && lastTicket==1,"empty scope does not create a ticket");
    check(canOfferFree(0,"america",{"next"},{},0) && !canOfferFree(0,"america",{"common0"},{},0) &&
        !canOfferFree(0,"america",{"next"},{},-1),"source excludes itself and can unlock its own successor");
    check(grantFree(0,"america",{"common4","common0"},{}) &&
        ticketOptions(lastTicket,0,"america")==std::vector<int>({4,0}),"short scope keeps order even with zero weight");
    auto ticket=lastTicket;
    unlocked[0].insert("common4");clearOfferCache();
    check(ticketOptions(ticket,0,"america")==std::vector<int>{0},"ownership changes preserve scope");
    enabled=false;
    check(!grantFree(0,"america",{},Rarity::Common) && ticketOptions(ticket,0,"america").empty(),"research switch blocks tickets");
    reset();
    check(lastTicket==0 && freeTickets.empty(),"scenario reset drops outstanding tickets");
}
void sharedFixture() {
    Research::reset();Research::sharedRaritySequence=true;
    Research::rarityWeights={5,4,1,0};Research::allowCardRefresh=true;
    for(const auto country:{"america","japan"}) for(int tier=0;tier<4;++tier) for(int i=0;i<20;++i) {
        Research::Option option;
        option.id=std::string(country)+std::to_string(tier)+"-"+std::to_string(i);
        option.rarity=static_cast<Research::Rarity>(tier);option.validChain=true;option.countries={country};
        Research::options.push_back(option);
    }
}
Research::Rarity boardRarity(int house,const char* country) {
    const auto& board=Research::offers(house,country);
    check(board.size()==3,"shared sequence has three eligible cards");
    auto rarity=Research::options[board.front()].rarity;
    check(std::all_of(board.begin(),board.end(),[&](int i){return Research::options[i].rarity==rarity;}),"one rarity per shared round");
    check(rarity!=Research::Rarity::Insane,"zero weight excluded from shared sequence");
    return rarity;
}
void sharedSequenceTests() {
    sharedFixture();
    std::vector<Research::Rarity> sequence;
    Research::award(0,1000);Research::award(1,1000);
    const auto slowBoard=ids(1,"japan");
    for(int round=0;round<10;++round) {
        sequence.push_back(boardRarity(0,"america"));
        auto option=Research::offers(0,"america").front();
        check(Research::refresh(0,"america",0,option) && boardRarity(0,"america")==sequence.back(),"refresh keeps common rarity");
        check(Research::completedRounds.size()==(round?1u:0u),"refresh and panel reads do not advance rounds");
        auto board=ids(0);
        Research::clearOfferCache();
        check(ids(0)==board,"shared board survives cache rebuild");
        option=Research::offers(0,"america").front();
        effectsSucceed=false;
        check(!Research::purchase(0,option,"america") && ids(0)==board,"failed shared purchase keeps round");
        effectsSucceed=true;
        check(Research::purchase(0,option,"america") && Research::completedRounds.at(0)==round+1u,"successful purchase advances once");
        check(!Research::purchase(0,option,"america") && Research::completedRounds.at(0)==round+1u,"duplicate purchase cannot advance round");
        check(ids(1,"japan")==slowBoard && !Research::completedRounds.contains(1),"fast player does not advance slow player");
    }
    check(std::set<Research::Rarity>(sequence.begin(),sequence.end()).size()>1,"shared sequence varies across rounds");
    Research::allowAIResearch=true;
    for(int round=0;round<10;++round) {
        check(boardRarity(1,"japan")==sequence[round],"different countries and acquisition order share nth rarity");
        check(Research::purchaseForAI(1,"japan",round*15)>=0 && Research::completedRounds.at(1)==round+1u,"AI follows shared sequence");
    }
    sharedFixture();
    check(boardRarity(1,"japan")==sequence.front(),"fresh scenario restarts shared sequence");
    const auto rarity=boardRarity(0,"america");
    // A free acquisition changes ownership but must not consume a numbered round.
    Research::unlocked[0].insert(Research::options.front().id);
    Research::savedOffers.erase(0);Research::clearOfferCache();
    check(boardRarity(0,"america")==rarity && Research::completedRounds.empty(),"free ownership does not advance shared sequence");
    for(auto& option:Research::options)
        if(option.countries==std::vector<std::string>{"america"} && option.rarity==rarity) option.prerequisites={"unavailable"};
    Research::savedOffers.erase(0);Research::clearOfferCache();
    check(boardRarity(0,"america")!=rarity && boardRarity(1,"japan")==rarity &&
        Research::completedRounds.empty(),"empty personal tier falls back without changing others or skipping rounds");
    Research::rarityWeights={INT_MAX,INT_MAX,INT_MAX,0};Research::savedOffers.clear();Research::clearOfferCache();
    check(!ids(1,"japan").empty(),"large shared weights do not overflow");
    Research::rarityWeights={0,0,0,0};Research::savedOffers.clear();Research::clearOfferCache();
    check(ids(0).empty() && ids(1,"japan").empty(),"all zero weights disable shared offers");
    sharedFixture();
    const auto original=boardRarity(0,"america");bool varied=false;
    const auto originalBoard=ids(0);
    const auto initialSeed=seed;
    Research::raritySequenceSeed=seed;
    seed^=0x12345678u;Research::savedOffers.clear();Research::clearOfferCache();
    check(boardRarity(0,"america")==original && ids(0)==originalBoard,"loaded sequence seed survives a different process seed");
    Research::raritySequenceSeed.reset();
    for(unsigned offset=1;offset<=32;++offset) {
        seed=initialSeed+offset;Research::savedOffers.clear();Research::clearOfferCache();
        varied|=boardRarity(0,"america")!=original;
    }
    check(varied,"different matches produce different common sequences");seed=initialSeed;
}
void randomResearchTests() {
    randomDraws=0;
    fixture();Research::rarityWeights={0,0,0,0};
    check(Research::grantRandom(0,"america",Research::Rarity::Rare)<0 && randomDraws==0,"random research filters countries and prerequisites before drawing");
    check(Research::grantRandom(1,"JAPAN",Research::Rarity::Rare)==8 && Research::owns(1,"rare") &&
        Research::unlockOrder.at(1)==std::vector<std::string>{"rare"} && Research::points.empty() && Research::completedRounds.empty(),"specified rarity grants free research despite zero ordinary weights");
    check(Research::grantRandom(1,"japan",Research::Rarity::Rare)<0 && randomDraws==0,"exhausted tier neither falls back nor consumes RNG");
    check(Research::grantRandom(0,"america",Research::Rarity::Common)==0,"random common grant");
    const auto owned=Research::unlocked;
    const auto order=Research::unlockOrder;
    effectsSucceed=false;
    check(Research::grantRandom(0,"america",Research::Rarity::Rare)<0 && Research::unlocked==owned &&
        Research::unlockOrder==order && Research::completedRounds.empty(),"failed random effect does not commit ownership or rounds");
    effectsSucceed=true;
    check(Research::grantRandom(0,"america",Research::Rarity::Rare)==10,"free research unlocks prerequisite chains");
    fixture();Research::award(0,100);
    const auto board=Research::offers(0,"america");
    check(Research::refresh(0,"america",0,board.front()),"prepare used refresh slot before grant");
    const auto mask=Research::refreshedSlots.at(0);
    check(Research::grantRandom(0,"america",Research::Rarity::Common)==0 && Research::refreshedSlots.at(0)==mask &&
        !Research::canRefresh(0,"america",0) && Research::balance(0)==100,"free research preserves points and used refresh allowance");
    check(std::find(Research::savedOffers.at(0).begin(),Research::savedOffers.at(0).end(),"common0")==Research::savedOffers.at(0).end(),"owned reward excluded from rebuilt ordinary offers");
    fixture();ids(0);
    Research::grantRandom(0,"america");const auto localBoard=ids(0);const auto localSeen=Research::seenOffers;
    fixture();Research::grantRandom(0,"america");
    check(ids(0)==localBoard && Research::seenOffers==localSeen,"random grant rebuild is independent of local panel history");
    sharedFixture();
    const auto rarity=boardRarity(0,"america");
    check(Research::grantRandom(0,"america",Research::Rarity::Insane)>=0 &&
        Research::completedRounds.empty() && boardRarity(0,"america")==rarity,"random reward does not consume shared rarity sequence");
    Research::reset();Research::rarityWeights={0,0,0,0};
    for(int tier=0;tier<4;++tier) {
        Research::Option option;option.id="tier"+std::to_string(tier);option.validChain=true;
        option.rarity=static_cast<Research::Rarity>(tier);Research::options.push_back(option);
    }
    blockedEffects.insert("tier0");randomPick=2;
    check(Research::grantRandom(0,"")==3 && lastPoolSize==3,"all rarities use equal project slots and exclude effects that cannot apply");
    blockedEffects.clear();randomPick=0;
    for(int tier=0;tier<3;++tier) check(Research::grantRandom(0,"")==tier,"all rarities drawn without duplicate ownership");
    const auto draws=randomDraws;
    check(Research::grantRandom(0,"")<0 && randomDraws==draws,"empty reward pool consumes no random draw");
    check(Research::grantRandom(-1,"")<0 && Research::grantRandom(1,"",Research::Rarity::Invalid)<0,"invalid random research arguments rejected");
    Research::enabled=false;
    check(Research::grantRandom(1,"")<0 && !Research::unlocked.contains(1) && randomDraws==draws,"disabled research rejects random grants");
}
void rewardChainTests() {
    using namespace Research;
    reset();rarityWeights={1,1,1,0};randomPick=0;
    auto add=[](const char* id,Rarity rarity,Effect effect,std::optional<Rarity> reward,int count) {
        Option o;o.id=id;o.rarity=rarity;o.effect=effect;o.reward={reward,count};o.validChain=true;options.push_back(o);
    };
    add("rare-upgrade",Rarity::Common,Effect::RandomResearch,Rarity::Rare,1);
    add("epic-upgrade",Rarity::Rare,Effect::RandomResearch,Rarity::Epic,1);
    add("chaos",Rarity::Epic,Effect::RandomResearch,std::nullopt,2);
    add("armor",Rarity::Epic,Effect::Passive,std::nullopt,1);
    add("heal",Rarity::Rare,Effect::Passive,std::nullopt,1);
    award(0,200);savedOffers[0]={"rare-upgrade"};
    check(purchase(0,0,"") && unlockOrder[0]==std::vector<std::string>{"rare-upgrade","epic-upgrade","chaos","armor","heal"},
        "reward cards chain through other cards and exclude every acquired card including self");
    check(balance(0)==100 && completedRounds[0]==1 && unlocked[0].size()==5 && !unlocked.contains(1),
        "entire chain charges and advances only original purchase for original owner");
    const auto owned=unlocked;const auto order=unlockOrder;
    clearOfferCache();offers(0,"");completedProjects(0);
    check(unlocked==owned && unlockOrder==order && !purchase(0,0,""),"cache reload and duplicate purchase do not issue rewards again");
    // Only a cycle remains: neither source can lead to a usable reward.
    options.resize(3);unlocked.clear();unlockOrder.clear();savedOffers.clear();clearOfferCache();
    check(!canApply(0,0) && !canApply(0,1) && !canApply(0,2) && grantRandom(0,"")<0,
        "reward-only cycles cannot enable themselves");
    // A terminal project makes the chain usable, even when fewer than two remain.
    add("last",Rarity::Epic,Effect::Passive,std::nullopt,1);
    unlocked[0]={"rare-upgrade","epic-upgrade"};
    check(grantRandom(0,"")==2 && owns(0,"last") && unlocked[0].size()==4,"short reward pool grants remaining project once");
    reset();add("bundle",Rarity::Common,Effect::Passive,std::nullopt,1);
    options[0].effectList=true;EffectSpec reward;reward.effect=Effect::RandomResearch;reward.reward={Rarity::Rare,1};
    options[0].effects={reward};add("reward",Rarity::Rare,Effect::Passive,std::nullopt,1);
    check(grantRandom(0,"",Rarity::Common)==0 && owns(0,"reward"),"combined project dispatches random reward after ownership");
    reset();
    for(int i=0;i<24;++i) {
        Option o;o.id="cycle"+std::to_string(i);o.validChain=true;o.effect=Effect::RandomResearch;options.push_back(o);
    }
    check(!canApply(0,0),"large reward-only cycle terminates without exponential revisits");
}
}
void Research::ensureReady() {}
bool Research::available(int option) {
    return enabled && option>=0 && option<static_cast<int>(options.size()) && options[option].rarity!=Rarity::Invalid;
}
bool Research::executeEffect(int house,int option) { return effectsSucceed && canApply(house,option); }
bool Research::canApply(int house,int option) {
    if(house<0 || !available(option) || blockedEffects.contains(options[option].id)) return false;
    const auto& o=options[option];
    auto applicable=[&](const auto& e) {return e.effect!=Effect::RandomResearch || canGrantRandom(house,option,"america",e.reward.rarity);};
    if(o.effectList) return std::any_of(o.effects.begin(),o.effects.end(),applicable);
    return applicable(o);
}
void Research::clearPresentationCaches(bool stopAudio) { ++presentationClears;stoppedAudio=stopAudio; }
namespace Research::Detail {
bool Runtime::eligibleHouse(HouseClass*) {return false;}
bool applyResearchEffect(HouseClass*,const EffectSpec&,int) {return false;}
bool hasReplacement(const HouseClass*,const EffectSpec&) {return false;}
void clearEffectCaches() { ++effectClears; }
unsigned matchSeed() { return seed; }
int randomIndex(int count) { ++randomDraws;lastPoolSize=count;return randomPick%count; }
void announceReward(int,int) {}
void notifyFreeResearch(int) {}
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

    fixture();
    Research::allowAIResearch=true;
    Research::award(1,99);
    check(Research::purchaseForAI(1,"japan",15)<0 && Research::savedOffers.empty(),"AI waits for enough points");
    Research::award(1,201);
    effectsSucceed=false;
    check(Research::purchaseForAI(1,"japan",30)<0 && Research::balance(1)==300,"AI retains points when effects cannot apply");
    effectsSucceed=true;
    check(Research::purchaseForAI(1,"japan",45)==8 && Research::owns(1,"rare") && Research::balance(1)==200,"AI buys an offered country project");
    check(Research::purchaseForAI(1,"japan",60)<0 && Research::balance(1)==200,"AI retains surplus when pool exhausted");
    buildingWeightsTest();
    freeChoiceTests();
    {
        using namespace Research;
        fixture();
        options.resize(3);
        options[0].excludes={"common1","common2"};
        options[0].rarity=Rarity::Epic;
        const std::vector<std::string> scope={"common0","common1"};
        check(grantFree(0,"america",scope,{}) && ticketOptions(lastTicket,0,"america").size()==2,
            "unowned exclusions permit both choices");
        check(!canOfferFree(0,"america",{"common0"},{},2) && !canGrantRandom(0,2,"america",Rarity::Epic),
            "reward preview accounts for ownership of the granting project");
        randomPick=1;
        check(grantRandom(0,"america",Rarity::Common)==2,"grant second exclusion");
        const int before=randomDraws;
        check(grantRandom(0,"america",Rarity::Epic)<0 && randomDraws==before,
            "owning any excluded ID removes the random grant before drawing");
        check(ids(0)==std::vector<std::string>{"common1"} &&
            ticketOptions(lastTicket,0,"america")==std::vector<int>{1} &&
            Detail::buildingOfferRows(1,0,scope)==std::vector<int>{1},
            "ordinary offers and outstanding reward boards drop excluded projects");
        check(grantRandom(1,"america",Rarity::Epic)==0 && canOfferFree(1,"america",{"common2"},{},-1),
            "exclusions are directional and ownership is per house");
        randomPick=0;
    }
    sharedSequenceTests();
    mixedPoolTests();
    repeatResearchTests();
    randomResearchTests();
    rewardChainTests();
    replacementTests();
    const auto generation=Research::uiGeneration;
    const auto cleared=effectClears, presented=presentationClears;
    Research::producers[1]={0,2};Research::buildingRewards[2]={0,0,true};
    Research::globalIncome.initialized=true;Research::expanded=true;
    Research::reset();
    check(Research::options.empty() && Research::unlocked.empty() && Research::unlockOrder.empty() &&
        Research::points.empty() && Research::savedOffers.empty() && Research::seenOffers.empty() &&
        Research::refreshedSlots.empty() && Research::producers.empty() && Research::buildingRewards.empty() &&
        !Research::globalIncome.initialized && !Research::expanded && Research::enabled && !Research::allowAIResearch &&
        !Research::sharedRaritySequence && Research::completedRounds.empty() && !Research::raritySequenceSeed,"scenario reset clears owned state");
    check(Research::uiGeneration==generation+1 && effectClears==cleared+1 &&
        presentationClears==presented+1 && stoppedAudio,"reset invalidates effect and presentation caches");
    std::puts("PASS independent research model: deterministic offers, shared rarity sequence, fallback, AI, reload, isolation, refresh, purchase, reset and configuration text");
}
