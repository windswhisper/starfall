#include "ResearchInternal.h"
#include "ConfigText.h"
#include <random>
#include <deque>
#include <tuple>

using std::string;
using std::vector;
using ConfigText::lower;
using ConfigText::includes;

namespace Research {
std::vector<Option> options;
std::array<std::string,5> soundFiles;
std::string freeRewardSound="builtin:available";
unsigned uiGeneration=0;
std::array<int,rarityCount> rarityWeights=defaultRarityWeights;
std::array<std::wstring,rarityCount> Detail::rarityNameOverrides;
std::array<std::optional<std::array<int,3>>,rarityCount> Detail::rarityColors;
const wchar_t* rarityName(Rarity rarity) {
    constexpr std::array<const wchar_t*,rarityCount> defaults={L"普通",L"稀有",L"史诗",L"疯狂"};
    const auto index=static_cast<size_t>(rarity);
    if(index>=defaults.size()) return L"未知";
    const auto& name=Detail::rarityNameOverrides[index];
    return name.empty()?defaults[index]:name.c_str();
}
std::wstring title;
bool enabled=true;
bool allowCardRefresh=true;
bool allowAIResearch=false;
bool showAvailableAnimation=true;
bool sharedRaritySequence=false;
bool mixedRarityPool=false;
bool announceSelection=true;
std::map<int,unsigned> refreshedSlots;
Unlocks unlocked, retired;
std::map<std::pair<int,string>,int> repeatCounts;
std::map<int,unsigned> completedRounds;
std::optional<unsigned> raritySequenceSeed;
std::map<int,std::vector<std::string>> unlockOrder;
std::map<int,int> points;
std::map<string,string> counterProjects;
CounterValues counterValues;
int counter(int house,const string& key) {
    if(key.starts_with('#')) {
        auto it=bondIndex.find(key.substr(1));
        return it==bondIndex.end()?0:bondCount(house,bonds[it->second]);
    }
    auto project=counterProjects.find(key);
    if(project==counterProjects.end() || !owns(house,project->second)) return 0;
    auto values=counterValues.find(house);
    if(values==counterValues.end()) return 0;
    auto found=values->second.find(key);
    return found==values->second.end()?0:found->second;
}
bool changeCounter(int house,const string& key,int delta) {
    auto project=counterProjects.find(key);
    if(project==counterProjects.end() || !owns(house,project->second)) return false;
    int previous=counter(house,key);
    int next=static_cast<int>(std::clamp<long long>(1LL*previous+delta,0,INT_MAX));
    if(next==previous) return false;
    if(next) counterValues[house][key]=next;
    else {
        auto group=counterValues.find(house);
        group->second.erase(key);
        if(group->second.empty()) counterValues.erase(group);
    }
    Detail::checkStages(house,&key);
    return true;
}
GlobalIncome globalIncome;
std::map<unsigned long,Producer> producers;
std::map<unsigned long,BuildingReward> buildingRewards;
std::map<unsigned long,FreeResearchTicket> freeTickets;
unsigned long lastTicket=0;
RewardId rewardPanel=0;
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
static bool excludedByOwnership(int house,const Option& project) {
    auto removed=retired.find(house);
    if(removed!=retired.end() && removed->second.contains(project.id)) return true;
    return std::any_of(project.excludes.begin(),project.excludes.end(),
        [house](const auto& id){return owns(house,id);});
}
int acquisitionCount(int house,const string& id) {
    if(!owns(house,id)) return 0;
    auto it=repeatCounts.find({house,id});
    return it==repeatCounts.end()?1:it->second;
}
bool ownershipAllows(int house,const Option& project) {
    return project.repeatable?acquisitionCount(house,project.id)<INT_MAX:!owns(house,project.id);
}
bool meetsCountryOrTech(int house,const Option& option,const string& country,int source) {
    if(option.countries.empty() && option.unlockWithTech.empty()) return true;
    if(includes(option.countries,"*") || includes(option.countries,country)) return true;
    if(!option.unlockWithTech.empty() && source>=0) {
        auto affected=memberBonds.find(options[source].id);
        if(affected!=memberBonds.end()) for(int index:affected->second) {
            const auto& bond=bonds[index];
            const int count=bondCount(house,bond,source);
            for(const auto& tier:bond.tiers) if(count>=tier.required)
                for(const auto& e:tier.effects) if(e.effect==Effect::Technology &&
                    includes(option.unlockWithTech,e.technology)) return true;
        }
    }
    if(!option.unlockWithTech.empty() && !bondProgress.empty()) {
        bool licensed=false;
        forBondEffects(house,[&](const auto& effect){if(effect.effect==Effect::Technology &&
            includes(option.unlockWithTech,effect.technology)) licensed=true;},source);
        if(licensed) return true;
    }
    if(!option.unlockWithTech.empty() && !stageProgress.empty()) {
        for(const auto& active:options) {
            if(!owns(house,active.id) || (source>=0 && includes(options[source].replaces,active.id))) continue;
            bool unlockedByStage=false;
            forStageEffects(house,active,[&](const auto& e){
                if(e.effect==Effect::Technology && includes(option.unlockWithTech,e.technology)) unlockedByStage=true;
            });
            if(unlockedByStage) return true;
        }
    }
    for(const auto& provider:option.techProviders) {
        if(source>=0) {
            const auto& grant=options[source];
            if(grant.id==provider) return true;
            if(includes(grant.replaces,provider)) continue;
        }
        if(owns(house,provider)) return true;
    }
    return false;
}
static bool eligibleAfterSource(int house,const Option& candidate,int source) {
    if(!candidate.validChain || (source<0 && excludedByOwnership(house,candidate))) return false;
    // Replaced bases were consumed by the first acquisition of this upgrade.
    auto consumedBase=[&](const string& id){return candidate.repeatable && owns(house,candidate.id) && includes(candidate.replaces,id);};
    if(source<0) return std::all_of(candidate.prerequisites.begin(),candidate.prerequisites.end(),
        [&](const auto& id){return owns(house,id) || consumedBase(id);});
    const auto& grant=options[source];
    const auto removed=retired.find(house);
    if((removed!=retired.end() && removed->second.contains(candidate.id)) || includes(grant.replaces,candidate.id)) return false;
    auto willOwn=[&](const auto& id){return id==grant.id || (owns(house,id) && !includes(grant.replaces,id));};
    return std::none_of(candidate.excludes.begin(),candidate.excludes.end(),willOwn) &&
        std::all_of(candidate.prerequisites.begin(),candidate.prerequisites.end(),[&](const auto& id){return willOwn(id) || consumedBase(id);});
}
void Detail::recordAcquisition(int house,int option) {
    const auto& project=options[option];
    auto& active=unlocked[house];
    auto& order=unlockOrder[house];
    for(const auto& id:project.replaces) {
        if(active.erase(id)) {
            auto values=counterValues.find(house);
            if(values!=counterValues.end()) {
                const auto prefix=id+".";
                std::erase_if(values->second,[&](const auto& entry){return entry.first.starts_with(prefix);});
                if(values->second.empty()) counterValues.erase(values);
            }
            std::erase_if(stageProgress,[&](const auto& entry){
                return std::get<0>(entry.first)==house && std::get<1>(entry.first)==id;
            });
            retired[house].insert(id);
            std::erase(order,id);
        }
    }
    if(active.insert(project.id).second) order.push_back(project.id);
    else {
        auto [it,firstRepeat]=repeatCounts.try_emplace({house,project.id},2);
        if(!firstRepeat && it->second<INT_MAX) ++it->second;
    }
    ++bondRevision;
}
std::vector<int> completedProjects(int house) {
    Research::ensureReady();
    std::vector<int> result;
    auto add=[&](const std::string& id) {
        if(!Research::owns(house,id)) return;
        for(int i=0;i<static_cast<int>(Research::options.size());++i)
            if(Research::options[i].id==id && std::find(result.begin(),result.end(),i)==result.end()) {
                result.push_back(i);break;
            }
    };
    auto it=Research::unlockOrder.find(house);
    if(it!=Research::unlockOrder.end()) for(const auto& id:it->second) add(id);
    // Saves created before purchase order was recorded retain all their icons.
    for(const auto& o:Research::options) add(o.id);
    return result;
}
bool purchase(int house,int option,const char* country) {
    if(balance(house)<price || !available(option) || !meetsPrerequisites(house,option) || !ownershipAllows(house,options[option])) return false;
    const auto& eligible=offers(house,country);
    if(std::find(eligible.begin(),eligible.end(),option)==eligible.end()) return false;
    if(!executeEffect(house,option)) return false;
    Detail::recordAcquisition(house,option);
    ++completedRounds[house];
    points[house]-=price;
    savedOffers.erase(house); seenOffers.erase(house); refreshedSlots.erase(house); clearOfferCache();
    grantProjectRewards(house,option,country);
    return true;
}
int purchaseForAI(int house,const char* country,unsigned frame) {
    if(!isEnabled() || !allowAIResearch || balance(house)<price) return -1;
    // Buying clears the offer cache. Shuffle a copy using synchronized inputs.
    auto candidates=offers(house,country);
    if(candidates.empty()) return -1;
    std::mt19937 rng(Detail::matchSeed() ^ (0x9E3779B9u*(house+1u)) ^ frame);
    std::shuffle(candidates.begin(),candidates.end(),rng);
    for(int option:candidates) if(purchase(house,option,country)) return option;
    return -1;
}
static int grantSelected(int house,const char* country,int option) {
    if(!executeEffect(house,option)) return -1;
    Detail::recordAcquisition(house,option);
    savedOffers.erase(house);seenOffers.erase(house);clearOfferCache();
    offers(house,country);
    grantProjectRewards(house,option,country);
    return option;
}
int grantNamed(int house,const char* country,const std::string& id) {
    if(house<0 || !isEnabled()) return -1;
    const auto countryId=lower(country?country:"");
    for(int i=0;i<static_cast<int>(options.size());++i) {
        const auto& o=options[i];
        if(o.id==id && ownershipAllows(house,o) && available(i) && meetsPrerequisites(house,i) && canApply(house,i) &&
            meetsCountryOrTech(house,o,countryId))
            return grantSelected(house,country,i);
    }
    return -1;
}
static std::set<std::pair<int,int>> rewardChain;
int grantRandom(int house,const char* country,std::optional<Rarity> rarity) {
    if(house<0 || !isEnabled() || (rarity && (*rarity<Rarity::Common || *rarity>=Rarity::Invalid))) return -1;
    const auto countryId=lower(country?country:"");
    vector<int> candidates;
    for(int i=0;i<static_cast<int>(options.size());++i) {
        const auto& option=options[i];
        if(!option.grantOnly && (!rarity || option.rarity==*rarity) && !rewardChain.contains({house,i}) && ownershipAllows(house,option) && available(i) && meetsPrerequisites(house,i) &&
            meetsCountryOrTech(house,option,countryId) && canApply(house,i))
            candidates.push_back(i);
    }
    if(candidates.empty()) return -1;
    const int option=candidates.size()==1?candidates.front():candidates[Detail::randomIndex(static_cast<int>(candidates.size()))];
    return grantSelected(house,country,option);
}
bool canGrantRandom(int house,int source,const char* country,std::optional<Rarity> rarity) {
    // Ignore ancestors while checking chains, so a cycle without a real reward
    // cannot enable a card. This check neither draws RNG nor changes ownership.
    static std::set<std::pair<int,int>> checking;
    static std::set<std::tuple<int,int,int>> visited;
    if(checking.empty()) visited.clear();
    if(!visited.emplace(house,source,rarity?static_cast<int>(*rarity):-1).second) return false;
    const auto key=std::pair{house,source};
    if(!checking.insert(key).second) return false;
    struct Guard {
        std::set<std::pair<int,int>>& set;std::pair<int,int> key;
        ~Guard() {set.erase(key);}
    } guard{checking,key};
    const auto countryId=lower(country?country:"");
    for(int i=0;i<static_cast<int>(options.size());++i) {
        const auto& candidate=options[i];
        if(!candidate.grantOnly && !checking.contains({house,i}) && (!rarity || candidate.rarity==*rarity) &&
           !rewardChain.contains({house,i}) && ownershipAllows(house,candidate) && available(i) && eligibleAfterSource(house,candidate,source) &&
           meetsCountryOrTech(house,candidate,countryId,source) && canApply(house,i)) return true;
    }
    return false;
}
void grantProjectRewards(int house,int option,const char* country) {
    struct Pending {int house;std::string country;ProjectReward reward;};
    static std::deque<Pending> pending;
    const bool root=rewardChain.empty();
    rewardChain.emplace(house,option);
    const auto& project=options[option];
    auto enqueue=[&](const auto& effect) {
        if(effect.effect==Effect::FreeResearch) grantFree(house,country,effect.reward.projects,effect.reward.rarity);
        if(effect.effect==Effect::RandomResearch) pending.push_back({house,country?country:"",effect.reward});
    };
    Detail::checkBonds(house,option);
    if(project.effectList) {for(const auto& effect:project.effects) enqueue(effect);}
    else enqueue(project);
    Detail::checkStages(house);
    if(!root) return;
    // Even repeatable projects can only be reached once per automatic chain.
    while(!pending.empty()) {
        auto next=std::move(pending.front());pending.pop_front();
        for(int n=0;n<next.reward.count;++n) {
            const int granted=grantRandom(next.house,next.country.c_str(),next.reward.rarity);
            if(granted<0) break;
            Detail::announceReward(next.house,granted);
        }
    }
    rewardChain.clear();
}
void reset() {
    clearPresentationCaches(true);
    Detail::rarityNameOverrides={};
    Detail::rarityColors={};
    mixedRarityPool=false;
    repeatCounts.clear();
    freeRewardSound="builtin:available";
    ++uiGeneration;soundFiles={};enabled=true;announceSelection=true;showAvailableAnimation=true;allowAIResearch=false;sharedRaritySequence=false;
    counterValues.clear();counterProjects.clear();stageProgress.clear();bondProgress.clear();bonds.clear();bondIndex.clear();memberBonds.clear();
    globalIncome={};
    raritySequenceSeed.reset();
    freeTickets.clear();lastTicket=0;
    unlocked.clear(); retired.clear(); unlockOrder.clear(); completedRounds.clear(); points.clear(); producers.clear(); buildingRewards.clear(); rewardPanel=0; savedOffers.clear(); clearOfferCache();
    seenOffers.clear(); refreshedSlots.clear(); options.clear(); Detail::indexStages(); expanded=false; pressed=pressedOption=-1; badgePageIndex=0;
}
}

bool Research::isEnabled() { ensureReady();return enabled; }
bool Research::meetsPrerequisites(int house,int option) {
    const auto& project=options[option];
    return eligibleAfterSource(house,project,-1);
}
// Opening the local UI must not consume the simulation RNG. Use the shared match
// seed plus the owner's index, and persist the chosen IDs rather than registry positions.
namespace {
struct OfferCache { string country; vector<int> rows,pool; };
std::map<int,OfferCache> offerCache;
struct BuildingBoard {
    const vector<string>* configuration=nullptr;
    vector<int> order;
};
std::map<std::pair<unsigned long,int>,BuildingBoard> buildingBoards;
std::map<unsigned long,OfferCache> ticketBoards;
template<class Weight>
int drawRarity(std::mt19937& rng,const std::array<Weight,Research::rarityCount>& weights) {
    unsigned long long total=0;
    for(auto weight:weights) if(weight>0) total+=weight;
    if(!total) return -1;
    auto draw=std::uniform_int_distribution<unsigned long long>(0,total-1)(rng);
    for(int i=0;i<Research::rarityCount;++i) if(weights[i]>0) {
        auto weight=static_cast<unsigned long long>(weights[i]);
        if(draw<weight) return i;
        draw-=weight;
    }
    return -1;
}
using RarityPools=std::array<vector<int>,Research::rarityCount>;
vector<int> drawCards(RarityPools& pools,const std::array<int,Research::rarityCount>& weights,std::mt19937& rng,size_t limit=Research::choices) {
    vector<int> rows;
    std::array<unsigned long long,Research::rarityCount> mass{};
    for(int tier=0;tier<Research::rarityCount;++tier) {
        auto& pool=pools[tier];
        if(pool.empty()) continue;
        std::shuffle(pool.begin(),pool.end(),rng);
        // Every remaining project contributes its own ticket weight in mixed mode.
        mass[tier]=static_cast<unsigned long long>(weights[tier])*(Research::mixedRarityPool?pool.size():1);
    }
    while(rows.size()<limit) {
        const int tier=drawRarity(rng,mass);
        if(tier<0) break;
        auto& pool=pools[tier];
        rows.push_back(pool.back());pool.pop_back();
        if(Research::mixedRarityPool) mass[tier]-=weights[tier];
        else if(pool.empty()) mass[tier]=0;
    }
    return rows;
}
// As in ordinary research, temporary effect requirements are checked when choosing a card.
// A project's own reward can offer a successor whose prerequisite it is about to satisfy.
vector<int> ticketCandidates(int house,const string& country,const vector<string>& projects,
                             std::optional<Research::Rarity> rarity,int source,bool firstOnly=false) {
    using namespace Research;
    vector<int> rows;
    const bool fixed=!projects.empty() && projects.size()<=choices;
    auto add=[&](int i) {
        const auto& o=options[i];
        if(o.grantOnly || i==source || !o.validChain || o.rarity==Rarity::Invalid || !ownershipAllows(house,o) ||
           !eligibleAfterSource(house,o,source) ||
           (rarity && o.rarity!=*rarity) || (!rarity && !fixed && rarityWeights[static_cast<int>(o.rarity)]<=0) ||
           !meetsCountryOrTech(house,o,country,source)) return;
        if(available(i)) rows.push_back(i);
    };
    if(projects.empty()) {
        for(int i=0;i<static_cast<int>(options.size());++i) {
            add(i);
            if(firstOnly && !rows.empty()) break;
        }
    } else for(const auto& key:projects) {
        auto it=std::find_if(options.begin(),options.end(),[&](const Option& o){return o.id==key;});
        if(it!=options.end()) add(static_cast<int>(it-options.begin()));
        if(firstOnly && !rows.empty()) break;
    }
    return rows;
}
}
void Research::clearOfferCache() { offerCache.clear(); buildingBoards.clear(); ticketBoards.clear(); Detail::clearEffectCaches(); }
const vector<int>& Research::Detail::buildingOfferRows(unsigned long id,int house,const vector<string>& configured) {
    auto& board=buildingBoards[{id,house}];
    if(board.configuration==&configured) return board.order;
    board.configuration=&configured;
    board.order.clear();
    std::array<vector<int>,rarityCount> pools;
    for(const auto& key:configured) {
        auto it=std::find_if(options.begin(),options.end(),[&](const Option& option){return option.id==key;});
        if(it==options.end() || it->grantOnly || it->rarity==Rarity::Invalid) continue;
        int index=static_cast<int>(it-options.begin());
        if(!it->replaces.empty() && !owns(house,key) && (!it->validChain || !std::all_of(it->replaces.begin(),it->replaces.end(),
            [house](const auto& id){return owns(house,id);}))) continue;
        if(!ownershipAllows(house,*it) || excludedByOwnership(house,*it) || !available(index)) continue;
        if(configured.size()<=choices) board.order.push_back(index);
        else pools[static_cast<int>(it->rarity)].push_back(index);
    }
    if(configured.size()<=choices) return board.order;
    // Each peer can reconstruct the board without opening the HUD or advancing game RNG.
    std::seed_seq seed{raritySequenceSeed.value_or(matchSeed()),static_cast<unsigned>(id),static_cast<unsigned>(house),0xBD76513Fu};
    std::mt19937 rng(seed);
    board.order=drawCards(pools,rarityWeights,rng);
    return board.order;
}
bool Research::canOfferFree(int house,const char* country,const vector<string>& projects,std::optional<Rarity> rarity,int source) {
    return house>=0 && isEnabled() && lastTicket!=ULONG_MAX && projects.size()<=SHRT_MAX+1u &&
        std::none_of(projects.begin(),projects.end(),[](const string& key){return key.empty() || key.size()>127;}) &&
        !ticketCandidates(house,lower(country?country:""),projects,rarity,source,true).empty();
}
bool Research::grantFree(int house,const char* country,const vector<string>& projects,std::optional<Rarity> rarity) {
    if(!canOfferFree(house,country,projects,rarity,-1)) return false;
    freeTickets.emplace(++lastTicket,FreeResearchTicket{house,projects,rarity});
    Detail::notifyFreeResearch(house);
    return true;
}
const vector<int>& Research::ticketOptions(unsigned long id,int house,const char* country) {
    static const vector<int> empty;
    auto it=freeTickets.find(id);
    if(!isEnabled() || it==freeTickets.end() || it->second.house!=house) return empty;
    auto key=lower(country?country:"");
    auto [entry,added]=ticketBoards.try_emplace(id);
    auto& board=entry->second;
    if(!added && board.country==key) return board.rows;
    const auto& ticket=it->second;
    board.country=key;
    board.rows=ticketCandidates(house,key,ticket.projects,ticket.rarity,-1);
    if(ticket.projects.empty() || ticket.projects.size()>choices) {
        RarityPools pools;
        for(int i:board.rows) pools[static_cast<int>(options[i].rarity)].push_back(i);
        auto weights=rarityWeights;
        if(ticket.rarity) weights[static_cast<int>(*ticket.rarity)]=1;
        std::seed_seq seed{raritySequenceSeed.value_or(Detail::matchSeed()),static_cast<unsigned>(id),static_cast<unsigned>(house),0xF7ACE013u};
        std::mt19937 rng(seed);
        board.rows=drawCards(pools,weights,rng);
    }
    return board.rows;
}
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
        int rarity=static_cast<int>(options[i].rarity);
        if(!options[i].grantOnly && rarity>=0 && rarity<rarityCount && rarityWeights[rarity]>0 && ownershipAllows(house,options[i]) && available(i) && meetsPrerequisites(house,i) && meetsCountryOrTech(house,options[i],countryId)) pools[rarity].push_back(i);
    }
    auto& ids=savedOffers[house];
    // Use only synchronized state, so opening the panel does not consume game RNG.
    const unsigned matchSeed=(sharedRaritySequence || mixedRarityPool)?raritySequenceSeed.value_or(Detail::matchSeed()):Detail::matchSeed();
    unsigned seed=matchSeed ^ (0x9E3779B9u*(house+1u));
    if(auto round=completedRounds.find(house);round!=completedRounds.end() &&
        std::any_of(options.begin(),options.end(),[](const Option& o){return o.repeatable;}))
        seed^=round->second*0x85EBCA6Bu;
    auto owned=unlocked.find(house);
    if(owned!=unlocked.end()) for(const auto& id:owned->second) {
        for(unsigned char c:id) seed=(seed^c)*16777619u;
        seed=(seed^0xffu)*16777619u;
    }
    std::mt19937 rng(seed);
    if(mixedRarityPool) {
        // Keep the weighted order past the three visible slots for unseen refreshes.
        result.pool=drawCards(pools,rarityWeights,rng,options.size());
    } else {
        int chosen=-1;
        for(const auto& id:ids) {
            for(int rarity=0;rarity<rarityCount && chosen<0;++rarity)
                for(int i:pools[rarity]) if(options[i].id==id) {chosen=rarity;break;}
            if(chosen>=0) break;
        }
        if(chosen<0 && sharedRaritySequence) {
            const auto progress=completedRounds.find(house);
            const unsigned round=progress==completedRounds.end()?0:progress->second;
            // Neither house, country, owned project IDs nor local panel activity may
            // influence the common sequence. Each player's purchase advances only them.
            std::mt19937 sharedRng(matchSeed ^ 0x6C8E9CF5u ^ (0x9E3779B9u*(round+1u)));
            chosen=drawRarity(sharedRng,rarityWeights);
            // A country can exhaust a tier or have no eligible projects in it. Keep
            // research usable with a personal fallback, without skipping a round.
            if(chosen>=0 && pools[chosen].empty()) chosen=-1;
        }
        if(chosen<0) {
            auto weights=rarityWeights;
            for(int i=0;i<rarityCount;++i) if(pools[i].empty()) weights[i]=0;
            chosen=drawRarity(rng,weights);
        }
        if(chosen<0) {ids.clear();return result.rows;}
        result.pool=std::move(pools[chosen]);
        if(result.pool.size()>choices) {
            std::mt19937 shuffleRng(seed ^ 0xA511E9B3u);
            std::shuffle(result.pool.begin(),result.pool.end(),shuffleRng);
        }
    }
    auto& seen=seenOffers[house];
    auto remember=[&](int i) {
        if(result.rows.size()<choices && std::find(result.rows.begin(),result.rows.end(),i)==result.rows.end())
            result.rows.push_back(i);
    };
    for(const auto& id:ids)
        for(int i:result.pool) if(options[i].id==id) {remember(i);break;}
    for(int i:result.pool) {
        if(result.rows.size()==choices) break;
        if(!mixedRarityPool || !seen.contains(options[i].id)) remember(i);
    }
    ids.clear();
    for(int i:result.rows) {ids.push_back(options[i].id);seenOffers[house].insert(options[i].id);}
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
