#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

// Shared normalization and diagnostics. These helpers do not own configuration state.
namespace ConfigText {
std::string trim(std::string value);
std::string lower(std::string value);
std::vector<std::string> split(const std::string& value);
int number(std::string value,bool percent=false);
std::optional<std::array<int,3>> rgb(const std::string& value);
bool includes(const std::vector<std::string>& values,const std::string& value);
std::wstring displayText(const std::string& value);
// Return the position after the matching close of a $(...) embedded in text.
template<class String>
size_t formulaEnd(const String& text,size_t start) {
    int depth=1;
    for(size_t pos=start+2;pos<text.size();++pos) {
        if(text[pos]=='(') ++depth;
        else if(text[pos]==')' && --depth==0) return pos+1;
    }
    return String::npos;
}
bool appendTooltipNumber(std::wstring& line,double value,int decimals);
void configLog(const std::string& message);
}
