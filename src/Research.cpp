#include "ResearchInternal.h"
#include "ConfigText.h"
#include <random>

using std::string;
using std::vector;
using ConfigText::lower;
using ConfigText::includes;

namespace Research {
std::vector<Option> options;
std::array<std::string,5> soundFiles;
unsigned uiGeneration=0;
std::array<int,rarityCount> rarityWeights=defaultRarityWeights;
std::wstring title;
bool enabled=true;
bool allowCardRefresh=true;
std::map<int,unsigned> refreshedSlots;
Unlocks unlocked;
std::map<int,std::vector<std::string>> unlockOrder;
std::map<int,int> points;
GlobalIncome globalIncome;
std::map<unsigned long,Producer> producers;
std::map<unsigned long,BuildingReward> buildingRewards;
unsigned long rewardPanel=0;
std::map<int,std::vector<std::string>> savedOffers;
std::map<int,std::set<std::string>> seenOffers;
bool expanded=false;
int pressed=-1,pressedOption=-1;
int badgePageIndex=0;

int balance(int house) {
    auto it=points.find(house);
    return it==points.end()?0:it->second;
}
void award(int house,int amount) {
    if(house<0 || amount<=0 || !isEnabled()) return;
    points[house]=static_cast<int>(std::min<long long>(INT_MAX,1LL*balance(house)+amount));
}
bool owns(int house,const std::string& id) {
    auto it=unlocked.find(house);
    return it!=unlocked.end() && it->second.contains(id);
}
bool purchase(int house,int option,const char* country) {
    if(balance(house)<price || !available(option) || !meetsPrerequisites(house,option) || owns(house,options[option].id)) return false;
    const auto& eligible=offers(house,country);
    if(std::find(eligible.begin(),eligible.end(),option)==eligible.end()) return false;
    if(!executeEffect(house,option)) return false;
    unlocked[house].insert(options[option].id);
    unlockOrder[house].push_back(options[option].id);
    points[house]-=price;
    savedOffers.erase(house); seenOffers.erase(house); refreshedSlots.erase(house); clearOfferCache();
    return true;
}
void reset() {
    clearPresentationCaches(true);
    ++uiGeneration;soundFiles={};enabled=true;
    globalIncome={};
    unlocked.clear(); unlockOrder.clear(); points.clear(); producers.clear(); buildingRewards.clear(); rewardPanel=0; savedOffers.clear(); clearOfferCache();
    seenOffers.clear(); refreshedSlots.clear(); options.clear(); expanded=false; pressed=pressedOption=-1; badgePageIndex=0;
}
}

bool Research::isEnabled() { ensureReady();return enabled; }
bool Research::meetsPrerequisites(int house,int option) {
    const auto& project=options[option];
    return project.validChain && std::all_of(project.prerequisites.begin(),project.prerequisites.end(),
        [house](const auto& id){return owns(house,id);});
}
// Opening the local UI must not consume the simulation RNG. Use the shared match
// seed plus the owner's index, and persist the chosen IDs rather than registry positions.
namespace {
struct OfferCache { string country; vector<int> rows,pool; };
std::map<int,OfferCache> offerCache;
}
void Research::clearOfferCache() { offerCache.clear(); Detail::clearEffectCaches(); }
const vector<int>& Research::offers(int house,const char* country) {
    static const vector<int> empty;
    if(!isEnabled()) return empty;
    const string countryId=lower(country?country:"");
    auto existing=offerCache.find(house);
    if(existing!=offerCache.end() && existing->second.country==countryId) return existing->second.rows;
    auto& result=offerCache[house];
    result.country=countryId;
    result.rows.clear();
    result.pool.clear();
    std::array<vector<int>,rarityCount> pools;
    for(int i=0;i<static_cast<int>(options.size());++i) {
        const auto& allowed=options[i].countries;
        int rarity=static_cast<int>(options[i].rarity);
        if(rarity>=0 && rarity<rarityCount && rarityWeights[rarity]>0 && !owns(house,options[i].id) && available(i) && meetsPrerequisites(house,i) && (allowed.empty() || includes(allowed,"*") || includes(allowed,countryId))) pools[rarity].push_back(i);
    }
    auto& ids=savedOffers[house];
    int chosen=-1;
    for(const auto& id:ids) {
        for(int rarity=0;rarity<rarityCount && chosen<0;++rarity)
            for(int i:pools[rarity]) if(options[i].id==id) {chosen=rarity;break;}
        if(chosen>=0) break;
    }
    // Use only synchronized state, so opening the panel does not consume game RNG.
    unsigned seed=Detail::matchSeed() ^ (0x9E3779B9u*(house+1u));
    auto owned=unlocked.find(house);
    if(owned!=unlocked.end()) for(const auto& id:owned->second) {
        for(unsigned char c:id) seed=(seed^c)*16777619u;
        seed=(seed^0xffu)*16777619u;
    }
    std::mt19937 rng(seed);
    if(chosen<0) {
        unsigned long long total=0;
        for(int i=0;i<rarityCount;++i) if(!pools[i].empty()) total+=static_cast<unsigned>(rarityWeights[i]);
        if(total) {
            auto draw=std::uniform_int_distribution<unsigned long long>(0,total-1)(rng);
            for(int i=0;i<rarityCount;++i) if(!pools[i].empty()) {
                auto weight=static_cast<unsigned>(rarityWeights[i]);
                if(draw<weight) {chosen=i;break;}
                draw-=weight;
            }
        }
    }
    if(chosen<0) {ids.clear();return result.rows;}
    auto& pool=pools[chosen];
    auto remember=[&](int option) {
        if(result.rows.size()<choices && std::find(pool.begin(),pool.end(),option)!=pool.end()
            && std::find(result.rows.begin(),result.rows.end(),option)==result.rows.end()) result.rows.push_back(option);
    };
    for(const auto& id:ids)
        for(int i:pool) if(options[i].id==id) { remember(i); break; }
    if(pool.size()>choices) {
        std::mt19937 shuffleRng(seed ^ 0xA511E9B3u);
        std::shuffle(pool.begin(),pool.end(),shuffleRng);
    }
    for(int i:pool) { if(result.rows.size()==choices) break; remember(i); }
    ids.clear();
    for(int i:result.rows) {ids.push_back(options[i].id);seenOffers[house].insert(options[i].id);}
    result.pool=std::move(pool);
    return result.rows;
}
bool Research::slotUsed(int house,int slot) {
    auto it=refreshedSlots.find(house);
    return slot>=0 && slot<choices && it!=refreshedSlots.end() && (it->second & (1u<<slot))!=0;
}
bool Research::canRefresh(int house,const char* country,int slot) {
    ensureReady();
    if(!allowCardRefresh || balance(house)<price || slot<0 || slot>=choices || slotUsed(house,slot)) return false;
    const auto& rows=offers(house,country);
    if(static_cast<size_t>(slot)>=rows.size()) return false;
    const auto& seen=seenOffers[house];
    const auto& pool=offerCache.at(house).pool;
    return std::any_of(pool.begin(),pool.end(),[&](int i){return !seen.contains(options[i].id);});
}
bool Research::refresh(int house,const char* country,int slot,int expected) {
    if(!canRefresh(house,country,slot)) return false;
    auto& board=offerCache.at(house);
    if(board.rows[slot]!=expected) return false;
    for(int option:board.pool) {
        if(seenOffers[house].contains(options[option].id)) continue;
        board.rows[slot]=option;
        savedOffers.at(house)[slot]=options[option].id;
        seenOffers[house].insert(options[option].id);
        refreshedSlots[house]|=1u<<slot;
        return true;
    }
    return false;
}
