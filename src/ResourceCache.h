#pragma once
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace Resources {
// Windows paths are case-insensitive for ASCII and accept either separator.
// Leave non-ASCII bytes intact so UTF-8 filenames remain valid.
inline std::string pathKey(std::string path) {
    for(char& c:path) {
        if(c>='A' && c<='Z') c+=('a'-'A');
        else if(c=='\\') c='/';
    }
    return path;
}

// Resident byte accounting is supplied by the caller (payload + key storage).
// Entry count also bounds empty/failed loads and container/control-block overhead.
// A borrowed value survives eviction/clear. Oversized values are returned for
// this use only, without evicting the cache's useful working set.
template<class Key,class Value> class Cache {
    struct Entry {
        std::shared_ptr<const Value> value;
        typename std::list<Key>::iterator recent;
        size_t bytes;
    };
    std::map<Key,Entry> entries_;
    std::list<Key> recent_;
    size_t bytes_=0;
public:
    const size_t maxEntries,maxBytes;
    Cache(size_t entries,size_t bytes):maxEntries(entries),maxBytes(bytes) {}
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    size_t size() const {return entries_.size();}
    size_t bytes() const {return bytes_;}
    bool contains(const Key& key) const {return entries_.contains(key);}
    void clear() {entries_.clear();recent_.clear();bytes_=0;}
    template<class Load,class Cost> std::shared_ptr<const Value> get(const Key& key,Load&& load,Cost&& cost) {
        if(auto found=entries_.find(key);found!=entries_.end()) {
            recent_.splice(recent_.begin(),recent_,found->second.recent);
            return found->second.value;
        }
        auto value=std::make_shared<const Value>(load());
        const size_t required=cost(key,*value);
        if(!maxEntries || required>maxBytes) return value;
        while(entries_.size()>=maxEntries || bytes_>maxBytes-required) {
            auto oldest=entries_.find(recent_.back());
            bytes_-=oldest->second.bytes;entries_.erase(oldest);recent_.pop_back();
        }
        recent_.push_front(key);
        try {entries_.emplace(key,Entry{value,recent_.begin(),required});}
        catch(...) {recent_.pop_front();throw;}
        bytes_+=required;return value;
    }
};
}
