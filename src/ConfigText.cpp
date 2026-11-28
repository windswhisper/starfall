#include "ConfigText.h"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <fstream>

using std::string;
using std::vector;
namespace ConfigText {
string trim(string s) {
    auto start = s.find_first_not_of(" \r\n\t");
    return start == string::npos ? "" : s.substr(start, s.find_last_not_of(" \r\n\t") - start + 1);
}
string lower(string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
vector<string> split(const string& s) {
    vector<string> out;
    size_t p = 0;
    while (p < s.size()) {
        auto end = s.find(',', p);
        auto item = lower(trim(s.substr(p, end - p)));
        if (!item.empty() && std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
        if (end == string::npos) break;
        p = end + 1;
    }
    return out;
}
int number(string s, bool percent) {
    if (percent && !s.empty() && s.back() == '%') s.pop_back();
    int n = 0;
    auto r = std::from_chars(s.data(), s.data()+s.size(), n);
    return r.ec == std::errc{} && r.ptr == s.data()+s.size() && n >= 0 ? n : -1;
}
bool includes(const vector<string>& list, const string& s) {
    return std::find(list.begin(),list.end(),s) != list.end();
}
std::optional<std::array<int,3>> rgb(const string& value) {
    std::array<int,3> channels{};
    size_t start=0;
    for(size_t i=0;i<channels.size();++i) {
        auto comma=value.find(',',start);
        channels[i]=number(trim(value.substr(start,comma-start)));
        if(channels[i]<0 || channels[i]>255 || (i<2)!=(comma!=string::npos)) return {};
        start=comma+1;
    }
    return channels;
}
std::wstring displayText(const string& value) {
    if(value.empty()) return {};
    UINT cp=CP_UTF8;
    int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,value.c_str(),-1,nullptr,0);
    if(n==0) { cp=CP_ACP; n=MultiByteToWideChar(cp,0,value.c_str(),-1,nullptr,0); }
    if(n<1) return {};
    std::wstring result(n,0);
    MultiByteToWideChar(cp,0,value.c_str(),-1,result.data(),n);
    result.pop_back();
    return result;
}
bool appendTooltipNumber(std::wstring& line,double value,int decimals) {
    char digits[320]; // Full fixed representation of a finite double, plus six decimals.
    auto [end,error]=std::to_chars(digits,digits+sizeof(digits),value,std::chars_format::fixed,decimals<0?2:decimals);
    if(error!=std::errc{}) return false;
    if(decimals<0) {
        while(end>digits && end[-1]=='0') --end;
        if(end>digits && end[-1]=='.') --end;
    }
    const char* first=digits;
    if(*first=='-' && std::none_of(first+1,static_cast<const char*>(end),[](char c){return c>='1' && c<='9';})) ++first;
    while(first!=end) line.push_back(*first++);
    return true;
}
void configLog(const string& message) {
    const string line="Starfall: "+message+"\n";
    OutputDebugStringA(line.c_str());
    std::ofstream("Starfall.log",std::ios::app)<<line;
}
}
