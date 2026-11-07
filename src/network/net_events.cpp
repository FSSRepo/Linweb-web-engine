#include "network/net_events.h"
#include "network/http_client.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
namespace linweb::net {
namespace {
std::mutex mutex;
std::atomic<int> next_id{1};
std::map<int, std::shared_ptr<std::atomic<bool>>> active;
std::vector<std::string> events;
std::string quote(const std::string& s) {
    static const char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string headers(const std::map<std::string,std::string>& h) {
    std::string s = "{"; bool first = true;
    for (const auto& p : h) { if (!first) s += ','; first = false; s += quote(p.first) + ':' + quote(p.second); }
    return s + '}';
}
void push(int id, const std::shared_ptr<std::atomic<bool>>& flag, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!*flag) events.push_back("{\"id\":" + std::to_string(id) + ',' + data + '}');
}
std::pair<int,std::shared_ptr<std::atomic<bool>>> create() {
    int id = next_id++; auto flag = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex); active[id] = flag; return {id, flag};
}
void finish(int id) { std::lock_guard<std::mutex> lock(mutex); active.erase(id); }
std::string head(const HttpResponse& r) { return "\"status\":" + std::to_string(r.status) + ",\"url\":" + quote(r.url) + ",\"headers\":" + headers(r.headers); }
}
int start_request(const std::string& method, const std::string& url, const std::map<std::string,std::string>& h, const std::string& body) {
    auto item = create(); int id = item.first; auto flag = item.second;
    std::thread([=] { try { HttpClient c; auto r = c.request(method,url,h,body); push(id,flag,"\"type\":\"response\"," + head(r) + ",\"body\":" + quote(r.body)); }
        catch (const std::exception& e) { push(id,flag,"\"type\":\"error\",\"message\":" + quote(e.what())); } finish(id); }).detach();
    return id;
}
int open_stream(const std::string& url, const std::map<std::string,std::string>& h) {
    auto item = create(); int id = item.first; auto flag = item.second;
    std::thread([=] { try { HttpClient c; c.stream(url,h,[&](const HttpResponse& r) { push(id,flag,"\"type\":\"open\"," + head(r)); },
        [&](const std::string& data) { push(id,flag,"\"type\":\"chunk\",\"data\":" + quote(data)); return !*flag; }); push(id,flag,"\"type\":\"end\""); }
        catch (const std::exception& e) { push(id,flag,"\"type\":\"error\",\"message\":" + quote(e.what())); } finish(id); }).detach();
    return id;
}
void cancel(int id) { std::lock_guard<std::mutex> lock(mutex); auto it = active.find(id); if (it != active.end()) *it->second = true; }
std::string take_events() { std::lock_guard<std::mutex> lock(mutex); std::string s = "["; for (size_t i=0;i<events.size();++i) { if(i) s+=','; s+=events[i]; } events.clear(); return s+']'; }
}
