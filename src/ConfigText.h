#pragma once
#include <string>
#include <vector>

// Shared normalization and diagnostics. These helpers do not own configuration state.
namespace ConfigText {
std::string trim(std::string value);
std::string lower(std::string value);
std::vector<std::string> split(const std::string& value);
int number(std::string value,bool percent=false);
bool includes(const std::vector<std::string>& values,const std::string& value);
std::wstring displayText(const std::string& value);
void configLog(const std::string& message);
}
