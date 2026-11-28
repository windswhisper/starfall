#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The INI reader already lowercases values. Compile once when rules are loaded.
class HealthFormula {
    enum class Op { Literal, Health, MaxHealth, SelfCost, SelfAmmo, SelfPassengers, TargetHealth, TargetMaxHealth, TargetCost, SenderHealth, SenderMaxHealth, SenderCost, SignalValue, Stack, Damage, Distance, Neg, Add, Sub, Mul, Div, Min, Max, Clamp, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, JumpIfFalse, Jump, Floor, SelfRank, GattlingStage, MaxGattlingStage, PowerOutput, PowerDrain, PowerSurplus, ResearchCounter };
    struct Instruction { Op op; double number; };
    static constexpr size_t limit=96;
    std::vector<Instruction> code;
    std::vector<std::string> counters;

    struct Parser {
        std::string_view input;
        std::vector<Instruction>& output;
        std::vector<std::string>& counters;
        void spaces() { input.remove_prefix(std::min(input.size(),input.find_first_not_of(" \t"))); }
        bool take(char c) {
            spaces();
            if(input.empty() || input.front()!=c) return false;
            input.remove_prefix(1);
            return true;
        }
        bool take(std::string_view token) {
            spaces();
            if(!input.starts_with(token)) return false;
            input.remove_prefix(token.size());return true;
        }
        bool emit(Op op,double number=0) {
            if(output.size()>=limit) return false;
            output.push_back({op,number});
            return true;
        }
        // Comparisons bind less tightly than arithmetic and cannot be chained.
        bool comparison(unsigned depth) {
            if(!sum(depth)) return false;
            Op op;
            if(take("<=")) op=Op::LessEqual;
            else if(take(">=")) op=Op::GreaterEqual;
            else if(take("==")) op=Op::Equal;
            else if(take("!=")) op=Op::NotEqual;
            else if(take('<')) op=Op::Less;
            else if(take('>')) op=Op::Greater;
            else return true;
            return sum(depth) && emit(op);
        }
        bool sum(unsigned depth) {
            if(!product(depth)) return false;
            for(;;) {
                Op op;
                if(take('+')) op=Op::Add;
                else if(take('-')) op=Op::Sub;
                else return true;
                if(!product(depth) || !emit(op)) return false;
            }
        }
        bool product(unsigned depth) {
            if(!operand(depth)) return false;
            for(;;) {
                Op op;
                if(take('*')) op=Op::Mul;
                else if(take('/')) op=Op::Div;
                else return true;
                if(!operand(depth) || !emit(op)) return false;
            }
        }
        bool operand(unsigned depth) {
            if(depth>24) return false;
            if(take('-')) return operand(depth+1) && emit(Op::Neg);
            if(take('+')) return operand(depth+1);
            if(take('(')) return comparison(depth+1) && take(')');
            if(input.empty()) return false;
            if((input.front()>='0' && input.front()<='9') || input.front()=='.') {
                double n;
                auto end=input.data()+input.size();
                auto parsed=std::from_chars(input.data(),end,n);
                if(parsed.ec!=std::errc{} || !std::isfinite(n)) return false;
                input.remove_prefix(parsed.ptr-input.data());
                return emit(Op::Literal,take('%') ? n/100 : n);
            }
            const auto len=input.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.");
            const auto name=input.substr(0,len);
            input.remove_prefix(name.size());
            if(name.starts_with("bond.") && name.ends_with(".count") && name.size()>11) {
                const auto id=name.substr(5,name.size()-11);
                if(id.find('.')!=id.npos) return false;
                counters.emplace_back("#"+std::string(id));
                return emit(Op::ResearchCounter,static_cast<double>(counters.size()-1));
            }
            if(name.starts_with("research.")) {
                auto key=name.substr(9);
                auto dot=key.find('.');
                if(dot==0 || dot==key.npos || dot+1>=key.size() || key.find('.',dot+1)!=key.npos) return false;
                counters.emplace_back(key);
                return emit(Op::ResearchCounter,static_cast<double>(counters.size()-1));
            }
            if(name=="self.health") return emit(Op::Health);
            if(name=="self.maxhealth") return emit(Op::MaxHealth);
            if(name=="self.cost") return emit(Op::SelfCost);
            if(name=="self.ammo") return emit(Op::SelfAmmo);
            if(name=="self.passengers") return emit(Op::SelfPassengers);
            if(name=="self.rank") return emit(Op::SelfRank);
            if(name=="self.gattlingstage") return emit(Op::GattlingStage);
            if(name=="self.maxgattlingstage") return emit(Op::MaxGattlingStage);
            if(name=="owner.poweroutput") return emit(Op::PowerOutput);
            if(name=="owner.powerdrain") return emit(Op::PowerDrain);
            if(name=="owner.powersurplus") return emit(Op::PowerSurplus);
            if(name=="target.health") return emit(Op::TargetHealth);
            if(name=="target.maxhealth") return emit(Op::TargetMaxHealth);
            if(name=="target.cost") return emit(Op::TargetCost);
            if(name=="sender.health") return emit(Op::SenderHealth);
            if(name=="sender.maxhealth") return emit(Op::SenderMaxHealth);
            if(name=="sender.cost") return emit(Op::SenderCost);
            if(name=="event.signalvalue") return emit(Op::SignalValue);
            if(name=="this.stack") return emit(Op::Stack);
            if(name=="event.damage") return emit(Op::Damage);
            if(name=="event.distance") return emit(Op::Distance);
            if(name=="floor") return take('(') && comparison(depth+1) && take(')') && emit(Op::Floor);
            if(name=="if") {
                if(!take('(') || !comparison(depth+1) || !take(',')) return false;
                const size_t branch=output.size();
                if(!emit(Op::JumpIfFalse) || !comparison(depth+1) || !take(',')) return false;
                const size_t end=output.size();
                if(!emit(Op::Jump)) return false;
                output[branch].number=static_cast<double>(output.size());
                if(!comparison(depth+1) || !take(')')) return false;
                output[end].number=static_cast<double>(output.size());
                return true;
            }
            if(name!="min" && name!="max" && name!="clamp") return false;
            if(!take('(') || !comparison(depth+1) || !take(',') || !comparison(depth+1)) return false;
            if(name=="clamp" && (!take(',') || !comparison(depth+1))) return false;
            return take(')') && emit(name=="min"?Op::Min:name=="max"?Op::Max:Op::Clamp);
        }
    };
public:
    struct Values { int current,maximum; std::optional<int> cost{}; };
    // Live-only variables deliberately do not invent snapshots for queued events.
    struct LiveState { int rank; std::optional<int> powerOutput{},powerDrain{}; int gattlingStage=0,maxGattlingStage=0; };
    bool usesLiveState() const { return std::any_of(code.begin(),code.end(),[](const auto& i){return i.op==Op::SelfRank || i.op==Op::GattlingStage || i.op==Op::MaxGattlingStage || i.op==Op::PowerOutput || i.op==Op::PowerDrain || i.op==Op::PowerSurplus;}); }
    bool empty() const { return code.empty(); }
    bool usesDamage() const { return std::any_of(code.begin(),code.end(),[](const auto& i){return i.op==Op::Damage;}); }
    bool usesDistance() const { return std::any_of(code.begin(),code.end(),[](const auto& i){return i.op==Op::Distance;}); }
    bool usesTargetValues() const { return std::any_of(code.begin(),code.end(),[](const auto& i){return i.op==Op::TargetHealth || i.op==Op::TargetMaxHealth || i.op==Op::TargetCost;}); }
    bool usesSignal() const { return std::any_of(code.begin(),code.end(),[](const auto& i){return i.op==Op::SenderHealth || i.op==Op::SenderMaxHealth || i.op==Op::SenderCost || i.op==Op::SignalValue;}); }
    bool validForResearch() const {
        return !empty() && std::all_of(code.begin(),code.end(),[](const Instruction& i) {
            switch(i.op) {
                case Op::Literal: case Op::ResearchCounter: case Op::Neg: case Op::Add: case Op::Sub:
                case Op::Mul: case Op::Div: case Op::Min: case Op::Max: case Op::Clamp: case Op::Floor:
                case Op::Less: case Op::LessEqual: case Op::Greater: case Op::GreaterEqual:
                case Op::Equal: case Op::NotEqual: case Op::Jump: case Op::JumpIfFalse: return true;
                default: return false;
            }
        });
    }
    const auto& counterNames() const {return counters;}
    using CounterReader=int (*)(int,const std::string&);
    std::optional<double> evaluateResearch(int house,CounterReader readCounter) const {
        return evaluate(0,0,0,std::nullopt,0,std::nullopt,nullptr,std::nullopt,std::nullopt,
            std::nullopt,std::nullopt,std::nullopt,std::nullopt,house,readCounter);
    }
    bool compile(std::string_view text) {
        code.clear();counters.clear();
        if(text.size()>512 || !text.starts_with("$(")) return false;
        Parser parser{text.substr(2),code,counters};
        if(parser.comparison(0) && parser.take(')')) {
            parser.spaces();
            if(parser.input.empty()) return true;
        }
        code.clear();
        return false;
    }
    std::optional<double> evaluate(int hp,int maximum,int damage=0,std::optional<double> distance=std::nullopt,int stacks=0,std::optional<Values> target=std::nullopt,bool* missing=nullptr,std::optional<int> selfCost=std::nullopt,std::optional<int> selfAmmo=std::nullopt,std::optional<int> selfPassengers=std::nullopt,std::optional<Values> sender=std::nullopt,std::optional<double> signalValue=std::nullopt,std::optional<LiveState> live=std::nullopt,int house=-1,CounterReader readCounter=nullptr) const {
        if(missing) *missing=false;
        if(empty()) return {};
        std::array<double,limit> stack;
        size_t used=0;
        for(size_t pc=0;pc<code.size();) {
            auto ins=code[pc++];
            switch(ins.op) {
                case Op::ResearchCounter:
                    if(!readCounter || house<0) {if(missing) *missing=true;return {};}
                    stack[used++]=readCounter(house,counters[static_cast<size_t>(ins.number)]);break;
                case Op::Literal: stack[used++]=ins.number; break;
                case Op::SelfRank:
                    if(!live) {if(missing) *missing=true;return {};}
                    stack[used++]=live->rank;break;
                case Op::GattlingStage: case Op::MaxGattlingStage:
                    if(!live) {if(missing) *missing=true;return {};}
                    stack[used++]=ins.op==Op::GattlingStage?live->gattlingStage:live->maxGattlingStage;
                    break;
                case Op::PowerOutput: case Op::PowerDrain: case Op::PowerSurplus:
                    if(!live || !live->powerOutput || !live->powerDrain) {if(missing) *missing=true;return {};}
                    stack[used++]=ins.op==Op::PowerOutput?double(*live->powerOutput):ins.op==Op::PowerDrain?double(*live->powerDrain):double(*live->powerOutput)-double(*live->powerDrain);
                    break;
                case Op::Health:
                    if(maximum<=0) {if(missing) *missing=true;return {};}
                    stack[used++]=std::clamp(hp,0,maximum);break;
                case Op::MaxHealth:
                    if(maximum<=0) {if(missing) *missing=true;return {};}
                    stack[used++]=maximum;break;
                case Op::SelfCost:
                    if(!selfCost) {if(missing) *missing=true;return {};}
                    stack[used++]=*selfCost;
                    break;
                case Op::SelfAmmo:
                    if(!selfAmmo) {if(missing) *missing=true;return {};}
                    stack[used++]=*selfAmmo;
                    break;
                case Op::SelfPassengers:
                    if(!selfPassengers) {if(missing) *missing=true;return {};}
                    stack[used++]=*selfPassengers;
                    break;
                case Op::TargetHealth: if(!target || target->maximum<=0) {if(missing) *missing=true;return {};} stack[used++]=std::clamp(target->current,0,target->maximum); break;
                case Op::TargetMaxHealth: if(!target || target->maximum<=0) {if(missing) *missing=true;return {};} stack[used++]=target->maximum; break;
                case Op::TargetCost:
                    if(!target || !target->cost) {if(missing) *missing=true;return {};}
                    stack[used++]=*target->cost;
                    break;
                case Op::SenderHealth: if(!sender || sender->maximum<=0) {if(missing) *missing=true;return {};} stack[used++]=std::clamp(sender->current,0,sender->maximum); break;
                case Op::SenderMaxHealth: if(!sender || sender->maximum<=0) {if(missing) *missing=true;return {};} stack[used++]=sender->maximum; break;
                case Op::SenderCost: if(!sender || !sender->cost) {if(missing) *missing=true;return {};} stack[used++]=*sender->cost; break;
                case Op::SignalValue: if(!signalValue) {if(missing) *missing=true;return {};} stack[used++]=*signalValue; break;
                case Op::Stack: stack[used++]=std::max(0,stacks); break;
                case Op::Damage: stack[used++]=std::max(0,damage); break;
                case Op::Distance: if(!distance || !std::isfinite(*distance) || *distance<0) {if(missing) *missing=true;return {};} stack[used++]=*distance; break;
                case Op::JumpIfFalse: if(stack[--used]==0) pc=static_cast<size_t>(ins.number); break;
                case Op::Jump: pc=static_cast<size_t>(ins.number); break;
                case Op::Neg: stack[used-1]=-stack[used-1]; break;
                case Op::Floor: stack[used-1]=std::floor(stack[used-1]); break;
                case Op::Clamp: {
                    double hi=stack[--used],lo=stack[--used];
                    if(lo>hi) return {};
                    stack[used-1]=std::clamp(stack[used-1],lo,hi);
                    break;
                }
                default: {
                    double right=stack[--used];
                    double& left=stack[used-1];
                    switch(ins.op) {
                        case Op::Add: left+=right; break;
                        case Op::Sub: left-=right; break;
                        case Op::Mul: left*=right; break;
                        case Op::Div: if(right==0) return {}; left/=right; break;
                        case Op::Min: left=std::min(left,right); break;
                        case Op::Max: left=std::max(left,right); break;
                        case Op::Less: left=left<right; break;
                        case Op::LessEqual: left=left<=right; break;
                        case Op::Greater: left=left>right; break;
                        case Op::GreaterEqual: left=left>=right; break;
                        case Op::Equal: left=left==right; break;
                        case Op::NotEqual: left=left!=right; break;
                        default: return {};
                    }
                    if(!std::isfinite(left)) return {};
                }
            }
        }
        return stack[0];
    }
};
