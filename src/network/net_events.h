#pragma once
#include <map>
#include <string>
namespace linweb::net {
int start_request(const std::string&, const std::string&, const std::map<std::string,std::string>&, const std::string&);
int open_stream(const std::string&, const std::map<std::string,std::string>&);
void cancel(int);
std::string take_events();
}
