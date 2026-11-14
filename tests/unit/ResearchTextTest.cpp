#include "../../src/ResearchText.h"
#include <cassert>
#include <cstdio>
int main() {
    ResearchText::Cache cache;
    for(int i=0;i<10000;++i) {
        auto label=L"科研点数 "+std::to_wstring(i)+L" / 100";
        assert(!cache.get(label.c_str(),16).alpha.empty());
        assert(cache.entries.size()<=ResearchText::Cache::maxEntries && cache.bytes<=ResearchText::Cache::maxBytes);
        // Keep a frequently drawn label alive while distinct numbers are evicted.
        const auto& hot=cache.get(L"科研",16);
        assert(&hot==&cache.get(L"科研",16));
    }
    assert(cache.entries.contains({16,L"科研"}) && !cache.entries.contains({16,L"科研点数 0 / 100"}));
    std::printf("PASS 10000 distinct labels, bounded LRU cache: %zu entries, %zu bytes\n",cache.entries.size(),cache.bytes);
}
