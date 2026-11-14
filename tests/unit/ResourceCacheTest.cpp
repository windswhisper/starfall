#include "ResourceCache.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <vector>

int main() {
    using Cache=Resources::Cache<std::string,std::vector<unsigned char>>;
    Cache cache(3,12);
    int loads=0;
    auto get=[&](const std::string& key,size_t bytes) {
        return cache.get(key,[&]{++loads;return std::vector<unsigned char>(bytes,42);},
            [](const auto&,const auto& value){return value.capacity();});
    };
    auto held=get("a",4);get("b",4);get("c",4);
    assert(get("a",4)==held && loads==3);
    get("d",4);
    assert(cache.contains("a") && !cache.contains("b") && cache.size()==3);
    get("e",8); // byte pressure evicts two old entries, not just one
    assert(cache.size()==2 && cache.bytes()==12 && cache.contains("d") && cache.contains("e"));
    assert(held->size()==4 && held->front()==42); // still in use after eviction
    auto large=get("large",13);
    assert(large->size()==13 && !cache.contains("large") && cache.size()==2 && cache.bytes()==12);
    bool threw=false;
    try {cache.get("error",[]()->std::vector<unsigned char>{throw std::runtime_error("fixture");},[](const auto&,const auto&){return 1u;});}
    catch(const std::runtime_error&) {threw=true;}
    assert(threw && cache.bytes()==12 && cache.size()==2);
    cache.clear();
    assert(cache.size()==0 && cache.bytes()==0 && held->front()==42);
    for(int i=0;i<10000;++i) {get("missing-"+std::to_string(i),0);assert(cache.size()<=3);}
    assert(cache.bytes()==0 && !cache.contains("missing-0"));
    assert(Resources::pathKey("Icons\\Panel.PNG")=="icons/panel.png");
    assert(Resources::pathKey("图标\\A.PNG")=="图标/a.png");
    puts("PASS cache byte and entry limits, LRU, borrowed lifetime, oversized use, failed loads, exceptions and path keys");
}
