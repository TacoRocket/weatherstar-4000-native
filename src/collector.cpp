#include <curl/curl.h>
#include "pages.hpp"
#include <libxml/parser.h>
#include <libxml/HTMLparser.h>
#include <nlohmann/json.hpp>
#include <sys/file.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
using json = nlohmann::json;
namespace fs = std::filesystem;
std::string env(const char* key, const char* fallback) { auto v=std::getenv(key); return v && *v ? v : fallback; }
size_t receive(char* data,size_t size,size_t count,void* target) { auto n=size*count; static_cast<std::string*>(target)->append(data,n); return n; }
std::string fetch(const std::string& url,long timeout=10) {
    CURL* c=curl_easy_init(); if(!c) throw std::runtime_error("curl init failed");
    std::string body; char error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(c,CURLOPT_URL,url.c_str()); curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"WeatherStar4000/1.0"); curl_easy_setopt(c,CURLOPT_TIMEOUT,timeout);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,5L); curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&body); curl_easy_setopt(c,CURLOPT_ERRORBUFFER,error);
    auto rc=curl_easy_perform(c); long code=0; curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code); curl_easy_cleanup(c);
    if(rc!=CURLE_OK || code!=200) throw std::runtime_error(url+": HTTP "+std::to_string(code)+" "+error);
    return body;
}
json get(const std::string& url) { return json::parse(fetch(url)); }
json read(const fs::path& path) { std::ifstream f(path); if(!f) throw std::runtime_error("cannot open "+path.string()); json j; f>>j; return j; }
void write(const fs::path& path,const std::string& content) { std::ofstream f(path,std::ios::binary); f.exceptions(std::ios::failbit|std::ios::badbit); f<<content; f.close(); }
std::string date(time_t t,const char* fmt="%Y-%m-%d",bool utc=false) { std::tm value{}; if(utc) gmtime_r(&t,&value); else localtime_r(&t,&value); char b[80]; strftime(b,sizeof b,fmt,&value); return b; }
void helper(const std::string& mode,const std::string& lat,const std::string& lon,const fs::path& output,const fs::path& input={}) {
    auto script=env("WEATHERSTAR_COLLECTOR_HELPER","/app/collector_helper.py");
    pid_t pid=fork(); if(pid<0) throw std::runtime_error("fork helper failed");
    if(pid==0) { execlp("python3","python3",script.c_str(),mode.c_str(),lat.c_str(),lon.c_str(),output.c_str(),input.c_str(),nullptr); _exit(127); }
    int result=0; if(waitpid(pid,&result,0)<0 || !WIFEXITED(result) || WEXITSTATUS(result)!=0) throw std::runtime_error(mode+" helper failed");
}
std::string content(xmlNode* node) { auto p=xmlNodeGetContent(node); std::string s=p?reinterpret_cast<char*>(p):""; xmlFree(p); return s; }
std::string child(xmlNode* node,const char* name) { for(auto p=node->children;p;p=p->next) if(p->type==XML_ELEMENT_NODE && xmlStrEqual(p->name,BAD_CAST name)) return content(p); return ""; }
std::string clean(const std::string& text) { auto doc=htmlReadMemory(text.data(),text.size(),nullptr,"UTF-8",HTML_PARSE_NONET|HTML_PARSE_NOERROR|HTML_PARSE_NOWARNING); if(!doc) return text; auto value=content(xmlDocGetRootElement(doc)); xmlFreeDoc(doc); return value; }
json headlines(const std::string& xml,time_t now) {
    auto doc=xmlReadMemory(xml.data(),xml.size(),nullptr,nullptr,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
    if(!doc) throw std::runtime_error("invalid RSS XML");
    json rows=json::array(); auto root=xmlDocGetRootElement(doc);
    if(root) for(auto channel=root->children;channel;channel=channel->next) if(xmlStrEqual(channel->name,BAD_CAST "channel"))
        for(auto item=channel->children;item;item=item->next) if(xmlStrEqual(item->name,BAD_CAST "item")) {
            auto title=clean(child(item,"title")), link=child(item,"link"), published=child(item,"pubDate");
            time_t t=curl_getdate(published.c_str(),nullptr);
            if(t<now-48*3600 || t>now || title.empty() || link.empty()) continue;
            rows.push_back({date(t,"%b %d")+": "+title,link}); if(rows.size()==10) break;
        }
    xmlFreeDoc(doc); return rows;
}
json traffic_feed(const std::string& xml,const std::string& source,const std::string& url,time_t now) {
    std::unique_ptr<xmlDoc,decltype(&xmlFreeDoc)> doc(
        xmlReadMemory(xml.data(),xml.size(),nullptr,nullptr,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING),xmlFreeDoc);
    if(!doc) throw std::runtime_error("invalid traffic RSS XML");
    auto root=xmlDocGetRootElement(doc.get());
    if(!root || !xmlStrEqual(root->name,BAD_CAST "rss")) throw std::runtime_error("traffic feed is not RSS");
    xmlNode* channel=nullptr;
    for(auto node=root->children;node;node=node->next)
        if(node->type==XML_ELEMENT_NODE && xmlStrEqual(node->name,BAD_CAST "channel")) { channel=node; break; }
    if(!channel) throw std::runtime_error("traffic RSS channel missing");
    auto published=child(channel,"pubDate");
    time_t timestamp=published.empty()?-1:curl_getdate(published.c_str(),nullptr);
    json items=json::array();
    for(auto node=channel->children;node;node=node->next)
        if(node->type==XML_ELEMENT_NODE && xmlStrEqual(node->name,BAD_CAST "item"))
            items.push_back({{"title",child(node,"title")},{"description",child(node,"description")},{"link",child(node,"link")}});
    return {{"source",source},{"feed_url",url},{"updated_at",timestamp<0?json():json(timestamp)},
            {"fetched_at",now},{"items",items}};
}
int main(int argc,char** argv) {
 try {
    const auto pages=weatherstar::selected_pages();
    auto enabled=[&](const std::string& page){ return weatherstar::page_enabled(pages,page); };
    fs::path state="/data";
    for(int i=1;i<argc;i++) { if(std::string(argv[i])=="--state" && i+1<argc) state=argv[++i]; else throw std::runtime_error("usage: weather-collect [--state DIR]"); }
    fs::create_directories(state); int lock=open((state/"collector.lock").c_str(),O_CREAT|O_RDWR,0644);
    if(lock<0) throw std::runtime_error("cannot open collector lock");
    if(flock(lock,LOCK_EX|LOCK_NB)!=0) { std::cerr<<"collector already running\n"; close(lock); return 0; }
    curl_global_init(CURL_GLOBAL_DEFAULT); time_t now=time(nullptr);
    json s=json::object(); if(fs::exists(state/"snapshot.json")) s=read(state/"snapshot.json");
    auto lat=env("WEATHERSTAR_LAT","38.9072"),lon=env("WEATHERSTAR_LON","-77.0369");
    const double latitude=std::stod(lat),longitude=std::stod(lon);
    const auto previous_location=s.value("location",json::object());
    const bool same_location=previous_location.is_object() &&
        previous_location.contains("lat") && previous_location["lat"].is_number() &&
        previous_location.contains("lon") && previous_location["lon"].is_number() &&
        previous_location["lat"].get<double>()==latitude &&
        previous_location["lon"].get<double>()==longitude;
    if(!same_location) {
        for(auto key:{"collector_point","collector_stations","weather_data","optional_weather","optional_updated","history_data","radar"}) s.erase(key);
        for(auto bucket:{"source_updated","collector_checked","errors"}) {
            if(!s.contains(bucket)) continue;
            for(auto key:{"point","stations","current","forecast","hourly","optional_forecast","air_quality","moon","history","radar"}) s[bucket].erase(key);
        }
    }
    for(auto key:{"weather_data","optional_weather","optional_updated","source_updated","errors","collector_checked"}) if(!s.contains(key)) s[key]=json::object();
    for(auto key:{"current","forecast","hourly"}) if(!s["weather_data"].contains(key)) s["weather_data"][key]=json::object();
    for(auto key:{"current","forecast","hourly","radar","history","news"}) if(!s["source_updated"].contains(key)) s["source_updated"][key]=0;
    for(auto key:{"forecast","air_quality","moon"}) if(!s["optional_updated"].contains(key)) s["optional_updated"][key]=0;
    if(!s.contains("history_data")) s["history_data"]={{"temperature",json::array()},{"precipitation",json::array()}};
    if(!s.contains("radar")) s["radar"]={{"frames",json::array()},{"updated_at",nullptr}};
    if(!s.contains("news")) s["news"]={{"source",env("WEATHERSTAR_NEWS_SOURCE","Local News")},{"headlines",json::array()},{"updated_at",nullptr}};
    s["location"]={{"city",env("WEATHERSTAR_LOCATION_LABEL","Washington")},{"state",env("WEATHERSTAR_LOCATION_STATE","DC")},{"lat",latitude},{"lon",longitude}};
    std::string query="latitude="+lat+"&longitude="+lon;
    auto run=[&](const std::string& key,long interval,const std::function<void()>& action) {
        if((key=="point" || key=="stations") && s["errors"].contains(key)) interval=300;
        if(now-s["collector_checked"].value(key,0L)<interval) return;
        s["collector_checked"][key]=now;
        try { action(); s["errors"].erase(key); } catch(const std::exception& e) { s["errors"][key]=e.what(); std::cerr<<key<<": "<<e.what()<<"\n"; }
    };
    json point=s.value("collector_point",json::object());
    run("point",86400,[&]{ auto fresh=get("https://api.weather.gov/points/"+lat+","+lon).at("properties"); point=fresh; s["collector_point"]=fresh; });
    if(!point.empty() && env("WEATHERSTAR_LOCATION_STATE","").empty()) {
        auto relative=point.value("relativeLocation",json::object()).value("properties",json::object());
        s["location"]["state"]=relative.value("state","DC");
    }
    std::vector<std::string> stations=s.value("collector_stations",std::vector<std::string>{});
    run("stations",86400,[&]{ if(point.empty()) throw std::runtime_error("point unavailable"); auto response=get(point.at("observationStations").get<std::string>()); std::vector<std::string> fresh;
        for(auto& f:response.at("features")) { auto id=f.at("properties").at("stationIdentifier").get<std::string>(); if(id.size()==4 && id[0]!='U' && id[0]!='C') fresh.push_back(id); if(fresh.size()==3) break; }
        if(fresh.empty()) throw std::runtime_error("no observation stations"); stations=fresh; s["collector_stations"]=stations;
    });
    run("current",300,[&]{ if(stations.empty()) throw std::runtime_error("observation station discovery unavailable"); json chosen; std::string failures; for(auto& station:stations) try {
        auto p=get("https://api.weather.gov/stations/"+station+"/observations/latest").at("properties"); if(p.empty()) continue;
        if(chosen.is_null()) chosen=p;
        if(p.contains("icon") && !p["icon"].is_null() && p.contains("temperature") && !p["temperature"].value("value",json()).is_null()) {chosen=p; break;}
      } catch(const std::exception& e) { failures+=e.what(); }
      if(chosen.is_null()) throw std::runtime_error("observations unavailable: "+failures);
      s["weather_data"]["current"]=chosen; s["source_updated"]["current"]=now;
    });
    for(auto key:{"forecast","hourly"}) run(key,300,[&]{ auto urlkey=std::string(key)=="hourly"?"forecastHourly":"forecast";
        if(point.empty()) throw std::runtime_error("NOAA point unavailable"); auto p=get(point.at(urlkey).get<std::string>()).at("properties");
        if(!p.contains("periods") || p["periods"].empty()) throw std::runtime_error("no forecast periods");
        s["weather_data"][key]=p; s["source_updated"][key]=now;
    });
    if(enabled("sunmoon") || enabled("uv")) run("optional_forecast",1800,[&]{ auto d=get("https://api.open-meteo.com/v1/forecast?"+query+"&daily=sunrise,sunset,uv_index_max&timezone=auto&forecast_days=7").at("daily"); json days=json::array();
        for(size_t i=0;i<d.at("time").size();i++) days.push_back({{"date",d["time"][i]},{"sunrise",d.at("sunrise").at(i)},{"sunset",d.at("sunset").at(i)},{"uv_index",d.at("uv_index_max").at(i)}});
        if(days.empty()) throw std::runtime_error("no daily values"); s["optional_weather"]["forecast"]={{"daily",days}}; s["optional_updated"]["forecast"]=now;
    });
    if(enabled("aqi")) run("air_quality",1800,[&]{ auto c=get("https://air-quality-api.open-meteo.com/v1/air-quality?"+query+"&current=us_aqi,pm10,pm2_5,carbon_monoxide,nitrogen_dioxide,sulphur_dioxide,ozone&timezone=auto").at("current");
        auto a=c.value("us_aqi",json()); std::string category="Unavailable";
        if(a.is_number()) {double n=a; category=n<=50?"Good":n<=100?"Moderate":n<=150?"Unhealthy for Sensitive Groups":n<=200?"Unhealthy":n<=300?"Very Unhealthy":"Hazardous";}
        s["optional_weather"]["air_quality"]={{"aqi",a},{"category",category},{"pm25",c.value("pm2_5",json())},{"pm10",c.value("pm10",json())},{"o3",c.value("ozone",json())}};
        s["optional_updated"]["air_quality"]=now;
    });
    if(enabled("sunmoon") && s["optional_weather"].value("moon",json::object()).value("date","")!=date(now)) run("moon",300,[&]{auto p=state/"moon.tmp"; helper("moon",lat,lon,p); auto moon=read(p); s["optional_weather"]["moon"]=moon; s["optional_updated"]["moon"]=now;});
    if(enabled("temperature-history") || enabled("precipitation-history")) run("history",3600,[&]{ auto result=get("https://api.open-meteo.com/v1/forecast?"+query+"&daily=temperature_2m_max,temperature_2m_min,precipitation_sum&temperature_unit=fahrenheit&precipitation_unit=inch&past_days=30&forecast_days=0&timezone=auto"); auto d=result.at("daily");
        auto today=date(now+result.value("utc_offset_seconds",0),"%Y-%m-%d",true); json temps=json::array(),precip=json::array();
        for(int i=int(d.at("time").size())-1;i>=0;i--) { if(d["time"][i].get<std::string>()>=today) continue;
            temps.push_back({d["time"][i],d.at("temperature_2m_max").at(i),d.at("temperature_2m_min").at(i)});
            precip.push_back({d["time"][i],d.at("precipitation_sum").at(i)});
        }
        if(temps.empty()) throw std::runtime_error("history empty"); s["history_data"]={{"temperature",temps},{"precipitation",precip}}; s["source_updated"]["history"]=now;
    });
    const auto news_url=env("WEATHERSTAR_NEWS_RSS_URL","");
    if(enabled("news") && !news_url.empty()) run("news",900,[&]{ auto rows=headlines(fetch(news_url,5),now);
        if(rows.empty()) throw std::runtime_error("no dated headlines within 48 hours"); s["news"]={{"source",env("WEATHERSTAR_NEWS_SOURCE","Local News")},{"headlines",rows},{"updated_at",now}}; s["source_updated"]["news"]=now;
    });
    if(enabled("traffic")) run("traffic",300,[&]{
        auto url=env("WEATHERSTAR_TRAFFIC_RSS_URL","");
        if(url.empty()) throw std::runtime_error("Set WEATHERSTAR_TRAFFIC_RSS_URL to your regional RSS feed");
        auto parsed=traffic_feed(fetch(url),env("WEATHERSTAR_TRAFFIC_SOURCE","Local Traffic"),url,now);
        s["traffic"]=std::move(parsed);
    });
    if(enabled("radar")) run("radar",300,[&]{ auto dir=state/"radar"/std::to_string(now); fs::create_directories(dir); json frames=json::array();
        for(int i=0;i<6;i++) { auto suffix=std::to_string(i); std::string failure; bool loaded=false;
            for(auto url:{"https://radar.weather.gov/ridge/standard/CONUS-LARGE_"+suffix+".gif","https://radar.weather.gov/ridge/standard/CONUS_"+suffix+".gif","https://radar.weather.gov/ridge/lite/N0R/CONUS_"+suffix+".png"}) try {
                auto bytes=fetch(url,3); if(bytes.size()<=1000) throw std::runtime_error("radar image too small");
                auto input=dir/("source-"+suffix),output=dir/("frame-"+suffix+".png"); write(input,bytes); helper("radar",lat,lon,output,input); fs::remove(input); frames.push_back(output.string()); loaded=true; break;
            } catch(const std::exception& e) { failure=e.what(); }
            if(!loaded && !failure.empty()) std::cerr<<"radar frame "<<i<<": "<<failure<<"\n";
        }
        if(frames.empty()) throw std::runtime_error("all radar frames unavailable"); s["radar"]={{"frames",frames},{"updated_at",now},{"source","NOAA radar"}}; s["source_updated"]["radar"]=now;
    });
    s["version"]=1; s["updated_at"]=now; write(state/"snapshot.json.tmp",s.dump(2)+"\n"); fs::rename(state/"snapshot.json.tmp",state/"snapshot.json");
    // Old generations remain for an hour so a renderer holding the prior snapshot can finish loading.
    if(fs::exists(state/"radar")) for(auto& entry:fs::directory_iterator(state/"radar")) {
        auto name=entry.path().filename().string(); if(!entry.is_directory() || name.empty() || name.find_first_not_of("0123456789")!=std::string::npos) continue;
        auto stamp=std::stoll(name); if(now-stamp>3600 && (!s["radar"]["updated_at"].is_number() || stamp!=s["radar"]["updated_at"].get<time_t>())) fs::remove_all(entry.path());
    }
    curl_global_cleanup(); close(lock); return 0;
 } catch(const std::exception& e) {std::cerr<<"collector: "<<e.what()<<"\n"; return 1;}
}
