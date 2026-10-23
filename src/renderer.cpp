// Layout and conversion formulas adapted from WeatherStar 4000 Python, MIT.
// Copyright (c) 2024 WeatherStar 4000 Python Contributors. See bundled LICENSE.
#include "pages.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using J = nlohmann::json;
using Surface = std::shared_ptr<SDL_Surface>;
static Surface own(SDL_Surface *p) {
  if (!p)
    throw std::runtime_error(SDL_GetError());
  return Surface(p, SDL_FreeSurface);
}
static Surface surface(int w = 640, int h = 480) {
  return own(
      SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888));
}
static const J empty = J::object();
static const J &get(const J &j, const std::string &k) {
  auto i = j.find(k);
  return i == j.end() ? empty : *i;
}
static std::string str(const J &j, const std::string &k,
                       std::string fallback = "") {
  const auto &v = get(j, k);
  return v.is_string() ? v.get<std::string>() : fallback;
}
static double num(const J &j, double fallback = NAN) {
  return j.is_number() ? j.get<double>() : fallback;
}
static std::string fmt(double v, int digits = 0) {
  if (!std::isfinite(v))
    return "N/A";
  std::ostringstream o;
  o << std::fixed << std::setprecision(digits) << v;
  return o.str();
}
static std::string integer(double v) {
  return std::isfinite(v) ? std::to_string(int(v)) : "N/A";
}
static double windMph(const J &measurement) {
  double value = num(get(measurement, "value"));
  std::string unit = str(measurement, "unitCode", "wmoUnit:km_h-1");
  if (unit == "wmoUnit:km_h-1")
    return value * .621371;
  if (unit == "wmoUnit:m_s-1")
    return value * 2.236936;
  if (unit == "wmoUnit:mi_h-1")
    return value;
  if (unit == "wmoUnit:kn")
    return value * 1.150779;
  return NAN;
}
static std::string upper(std::string s) {
  for (char &c : s)
    c = std::toupper((unsigned char)c);
  return s;
}
static std::string datefmt(std::time_t t, const char *f) {
  std::tm tm{};
  localtime_r(&t, &tm);
  char b[100];
  std::strftime(b, sizeof b, f, &tm);
  return b;
}
static std::tm iso(const std::string &s) {
  std::tm t{};
  std::istringstream i(s);
  i >> std::get_time(&t, s.size() > 10 ? "%Y-%m-%dT%H:%M:%S" : "%Y-%m-%d");
  t.tm_isdst = -1;
  return t;
}
static std::string isofmt(std::string s, const char *f) {
  if (s.size() < 10)
    return "Unavailable";
  auto t = iso(s);
  auto e = std::mktime(&t);
  return datefmt(e, f);
}
static std::string compass(double d) {
  static const char *a[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                            "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  return std::isfinite(d) ? a[(int((d + 11.25) / 22.5) % 16 + 16) % 16] : "";
}
static SDL_Color white{255, 255, 255, 255}, yellow{255, 255, 0, 255},
    cyan{0, 255, 255, 255}, blue{128, 128, 255, 255};
struct Icon {
  std::vector<Surface> frames;
  std::vector<int> delays;
  int duration = 0;
};
struct Placement {
  std::string name;
  int x, y, w, h;
};
class Renderer {
  std::string assets, snapshot;
  J data = J::object();
  std::filesystem::file_time_type modified{};
  bool loaded = false;
  std::map<std::string, TTF_Font *> fonts;
  std::map<std::string, Surface> textCache, images;
  size_t textBytes = 0;
  std::map<std::string, Icon> icons;
  size_t iconBytes = 0;
  std::array<Surface, 14> pages;
  std::array<std::vector<Placement>, 14> placements;
  std::vector<Surface> radar;
  std::vector<std::string> newsLines, hourLines, ticker;
  std::vector<std::pair<std::string, bool>> trafficLines;
  std::array<std::vector<std::array<std::string, 3>>, 2> history;
  Surface target, frame;
  double elapsed = 0;
  int building = 0;
  std::vector<int> enabledPages;
  TTF_Font *font(std::string name) {
    auto i = fonts.find(name);
    if (i != fonts.end())
      return i->second;
    throw std::runtime_error("Unknown font " + name);
  }
  Surface text(const std::string &s, std::string f, SDL_Color c) {
    std::string key = f + ":" + std::to_string(c.r) + "," +
                      std::to_string(c.g) + "," + std::to_string(c.b) + ":" + s;
    auto i = textCache.find(key);
    if (i != textCache.end())
      return i->second;
    auto p =
        own(TTF_RenderUTF8_Blended(font(f), s.empty() ? " " : s.c_str(), c));
    size_t bytes = p->pitch * p->h;
    if (textBytes + bytes > 16 * 1024 * 1024) {
      textCache.clear();
      textBytes = 0;
    }
    textBytes += bytes;
    textCache[key] = p;
    return p;
  }
  void blit(Surface p, int x, int y) {
    SDL_Rect r{x, y, 0, 0};
    if (SDL_BlitSurface(p.get(), nullptr, target.get(), &r) != 0)
      throw std::runtime_error("Surface composition failed: " +
                               std::string(SDL_GetError()));
  }
  void txt(std::string s, int x, int y, std::string f = "normal",
           SDL_Color c = white, int align = 0) {
    auto p = text(s, f, c);
    if (align == 1)
      x -= p->w / 2;
    if (align == 2)
      x -= p->w;
    blit(p, x, y);
  }
  void centered(std::string s, int x, int y, std::string f = "normal",
                SDL_Color c = white) {
    auto p = text(s, f, c);
    blit(p, x - p->w / 2, y - p->h / 2);
  }
  void rect(int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect r{x, y, w, h};
    SDL_FillRect(target.get(), &r, SDL_MapRGB(target->format, c.r, c.g, c.b));
  }
  Surface image(std::string path, int w = 0, int h = 0) {
    std::string key = path + ":" + std::to_string(w) + ":" + std::to_string(h);
    auto i = images.find(key);
    if (i != images.end())
      return i->second;
    SDL_Surface *raw = IMG_Load(path.c_str());
    if (!raw) {
      std::cerr << "Image unavailable " << path << ": " << IMG_GetError()
                << "\n";
      return {};
    }
    auto source = own(raw);
    auto p = own(
        SDL_ConvertSurfaceFormat(source.get(), SDL_PIXELFORMAT_ARGB8888, 0));
    if (w) {
      auto q = surface(w, h);
      SDL_FillRect(q.get(), nullptr, 0);
      if (SDL_BlitScaled(p.get(), nullptr, q.get(), nullptr) != 0)
        throw std::runtime_error("Image scaling failed for " + path + ": " +
                                 SDL_GetError());
      p = q;
    }
    images[key] = p;
    return p;
  }
  std::vector<std::string> wrap(std::string s, int width, std::string f) {
    std::istringstream in(s);
    std::string word, line;
    std::vector<std::string> out;
    while (in >> word) {
      std::string candidate = line.empty() ? word : line + " " + word;
      int w = 0;
      TTF_SizeUTF8(font(f), candidate.c_str(), &w, nullptr);
      if (w > width && !line.empty()) {
        out.push_back(line);
        line = word;
      } else
        line = candidate;
    }
    if (!line.empty())
      out.push_back(line);
    return out;
  }
  void wrapped(std::string s, int x, int y, int width, std::string f, int step,
               int limit, bool center = false) {
    auto lines = wrap(s, width, f);
    for (int i = 0; i < int(lines.size()) && i < limit; i++)
      if (center)
        centered(lines[i], x, y + i * step, f);
      else
        txt(lines[i], x, y + i * step, f);
  }
  std::string iconName(std::string url) {
    bool night = url.find("/night/") != std::string::npos;
    auto slash = url.rfind('/');
    std::string name = url.substr(slash == std::string::npos ? 0 : slash + 1);
    name = name.substr(0, name.find_first_of("?,"));
    std::map<std::string, std::string> m{
        {"skc", night ? "Clear" : "Sunny"},
        {"few", night ? "Mostly-Clear" : "Sunny"},
        {"sct", night ? "Partly-Clear" : "Partly-Cloudy"},
        {"bkn", "Cloudy"},
        {"ovc", "Cloudy"},
        {"rain", "Rain"},
        {"rain_showers", "Shower"},
        {"tsra", "Thunderstorm"},
        {"snow", "Light-Snow"},
        {"fog", "Fog"},
        {"wind", "Windy"}};
    return m.count(name) ? m[name] : "No-Data";
  }
  void place(std::string url, int cx, int cy, int w, int h) {
    auto name = iconName(url);
    placements[building].push_back({name, cx, cy, w, h});
  }
  void header(int bg, std::string top, std::string bottom, bool noaa = false) {
    auto b =
        image(assets + "/backgrounds/" + std::to_string(bg) + ".png", 640, 480);
    if (b)
      blit(b, 0, 0);
    else
      rect(0, 0, 640, 480, {0, 16, 64, 255});
    auto logo = image(assets + "/logos/logo-corner.png");
    if (logo)
      blit(logo, 50, 25);
    txt(upper(top), 170, 27, "title", yellow);
    txt(upper(bottom), 170, 53, "title", yellow);
    if (noaa) {
      auto p = image(assets + "/logos/noaa.gif");
      if (p)
        blit(p, 356, 39);
    }
  }
  void unavailable(std::string s = "Data unavailable") {
    centered(s, 320, 230);
  }
  const J &weather(std::string k) { return get(get(data, "weather_data"), k); }
  const J &optional(std::string k) {
    return get(get(data, "optional_weather"), k);
  }
  void footer(std::string key, std::string provider) {
    auto t = num(get(get(data, "optional_updated"), key));
    txt(provider +
            (std::isfinite(t) && t > 0
                 ? " | Updated " + datefmt(std::time_t(t), "%b %d %I:%M %p")
                 : " | Waiting for data"),
        60, 380, "tiny");
  }
  void rows(const std::vector<std::pair<std::string, std::string>> &r, int x,
            int valueX, int y, int step) {
    for (auto &[label, value] : r) {
      txt(label, x, y, "normal", yellow);
      txt(value, valueX, y);
      y += step;
    }
  }
  void current() {
    header(1, "Current", "Conditions", true);
    const auto &c = weather("current");
    if (c.empty()) {
      unavailable();
      return;
    }
    auto val = [&](std::string k) { return num(get(get(c, k), "value")); };
    double temp = val("temperature");
    centered(integer(temp * 1.8 + 32) + "°", 191, 140, "large");
    centered((str(c, "textDescription").empty() ? std::string("Unavailable")
                                                : str(c, "textDescription"))
                 .substr(0, 15),
             191, 190, "extended");
    place(str(c, "icon"), 191, 260, 86, 75);
    txt("Wind:", 74, 320, "extended");
    double wind = val("windSpeed");
    txt(!std::isfinite(wind) ? "N/A"
        : wind == 0          ? "Calm"
                             : compass(val("windDirection")) + " " +
                                   integer(windMph(get(c, "windSpeed"))),
        309, 320, "extended", white, 2);
    if (std::isfinite(val("windGust")))
      txt("Gusts to " + integer(windMph(get(c, "windGust"))), 309, 355,
          "normal", white, 2);
    txt(str(get(data, "location"), "city").substr(0, 20), 321, 100, "normal",
        yellow);
    std::vector<std::pair<std::string, std::string>> r;
    if (std::isfinite(val("relativeHumidity")))
      r.push_back({"Humidity:", integer(val("relativeHumidity")) + "%"});
    if (std::isfinite(val("dewpoint")))
      r.push_back({"Dewpoint:", integer(val("dewpoint") * 1.8 + 32) + "°"});
    double ceiling = NAN;
    for (auto &l : get(c, "cloudLayers"))
      if (str(l, "amount") == "BKN" || str(l, "amount") == "OVC") {
        ceiling = num(get(get(l, "base"), "value"));
        if (std::isfinite(ceiling))
          break;
      }
    r.push_back({"Ceiling:", !std::isfinite(ceiling) || ceiling == 0
                                 ? "Unlimited"
                                 : integer(ceiling * 3.28084) + " ft"});
    double vis = val("visibility") * .000621371;
    if (std::isfinite(vis))
      r.push_back({"Visibility:", vis >= 10 ? "10 mi" : fmt(vis, 1) + " mi"});
    if (std::isfinite(val("barometricPressure")))
      r.push_back(
          {"Pressure:", fmt(val("barometricPressure") * .0002953, 2) + "\""});
    if (temp > 26 && std::isfinite(val("heatIndex")))
      r.push_back({"Heat Index:", integer(val("heatIndex") * 1.8 + 32) + "°"});
    else if (temp < 10 && std::isfinite(val("windChill")))
      r.push_back({"Wind Chill:", integer(val("windChill") * 1.8 + 32) + "°"});
    int y = 130;
    for (auto &[k, v] : r) {
      txt(k, 341, y);
      txt(v, 566, y, "normal", white, 2);
      y += 36;
    }
  }
  void local() {
    header(2, "Local", "Forecast", true);
    const auto &p = get(weather("forecast"), "periods");
    if (p.size() < 3) {
      unavailable("Forecast unavailable");
      return;
    }
    int centers[] = {120, 320, 520};
    for (int i = 0; i < 3; i++) {
      auto &q = p[i];
      std::string name = str(q, "name");
      if (i == 0)
        name = name.find("Night") != std::string::npos || name == "Tonight" ||
                       name == "Overnight"
                   ? "TONIGHT"
                   : "TODAY";
      else if (i == 1)
        name = "TOMORROW";
      else {
        name = name.substr(0, name.find(' '));
        name = upper(name).substr(0, 9);
      }
      centered(name, centers[i], 120, "extended", yellow);
      centered(fmt(num(get(q, "temperature"))) + "°", centers[i], 150);
      wrapped(str(q, "detailedForecast"), centers[i], 180, 160, "forecast", 18,
              10, true);
    }
  }
  void extended() {
    header(3, "Extended", "Forecast");
    const auto &p = get(weather("forecast"), "periods");
    int count = std::min(3, int(p.size() / 2));
    if (!count) {
      unavailable();
      return;
    }
    int gap = (640 - 155 * count) / (count + 1);
    for (int n = 0; n < count; n++) {
      auto &day = p[n * 2];
      auto &night = p[n * 2 + 1];
      int x = gap + n * (155 + gap), cx = x + 77;
      std::string name = str(day, "name");
      if (name == "Tonight" || name == "Overnight")
        name = "TONIGHT";
      else if (name == "Today" || name == "This Morning" ||
               name == "This Afternoon")
        name = "TODAY";
      else
        name = upper(name).substr(0, 3);
      centered(name, cx, 120, "extended", yellow);
      place(str(day, "icon"), cx, 180, 100, 75);
      wrapped(str(day, "shortForecast"), cx, 240, 145, "small", 25, 2, true);
      bool daytime = get(day, "isDaytime") == true;
      double low = num(get(daytime ? night : day, "temperature")),
             high = num(get(daytime ? day : night, "temperature"));
      centered("Lo", x + 44, 310, "small", blue);
      centered(fmt(low) + "°", x + 44, 335);
      centered("Hi", x + 111, 310, "small", yellow);
      centered(fmt(high) + "°", x + 111, 335);
    }
  }
  void hourly() {
    header(4, "Hourly", "Forecast");
    txt("TIME  TEMP  CONDITIONS", 65, 125, "small", yellow);
    auto p = get(weather("hourly"), "periods");
    if (p.empty())
      p = get(weather("forecast"), "periods");
    hourLines.clear();
    for (size_t i = 0; i < p.size() && i < 24; i++) {
      std::string hour = isofmt(str(p[i], "startTime"), "%I %p");
      if (hour.size() && hour[0] == '0')
        hour.erase(0, 1);
      std::ostringstream line;
      line << std::setw(7) << hour << std::setw(3)
           << fmt(num(get(p[i], "temperature"))) << "° "
           << str(p[i], "shortForecast").substr(0, 35);
      hourLines.push_back(line.str());
    }
    if (hourLines.empty())
      unavailable("Hourly forecast unavailable");
  }
  void weekend() {
    header(4, "Weekend", "Forecast");
    auto &p = get(weather("forecast"), "periods");
    bool any = false;
    for (int d = 0; d < 2; d++) {
      std::string day = d ? "Sunday" : "Saturday";
      int x = d ? 340 : 60, y = 180, n = 0;
      for (auto &q : p) {
        if (str(q, "name").find(day) == std::string::npos || n >= 2)
          continue;
        if (!n)
          centered(upper(day), x + 130, 145, "extended", yellow);
        any = true;
        n++;
        txt(str(q, "name").find("Night") == std::string::npos ? "DAY" : "NIGHT",
            x + 10, y, "normal", cyan);
        txt(fmt(num(get(q, "temperature"))) + "°", x + 10, y + 25);
        place(str(q, "icon"), x + 100, y + 30, 60, 60);
        y += 50;
        auto lines = wrap(str(q, "shortForecast"), 240, "tiny");
        for (size_t i = 0; i < lines.size() && i < 3; i++) {
          txt(lines[i], x + 10, y, "tiny");
          y += 18;
        }
        y += 15;
      }
    }
    if (!any)
      unavailable("Weekend forecast not available");
  }
  void sunmoon() {
    header(5, "Sun & Moon", "Local Almanac");
    J day;
    for (auto &d : get(optional("forecast"), "daily"))
      if (str(d, "date") == datefmt(time(nullptr), "%Y-%m-%d"))
        day = d;
    std::vector<std::pair<std::string, std::string>> r;
    if (!str(day, "sunrise").empty() && !str(day, "sunset").empty()) {
      auto rise = iso(str(day, "sunrise")), set = iso(str(day, "sunset"));
      int minutes =
          int(std::difftime(std::mktime(&set), std::mktime(&rise)) / 60);
      r = {{"Sunrise", isofmt(str(day, "sunrise"), "%I:%M %p")},
           {"Sunset", isofmt(str(day, "sunset"), "%I:%M %p")},
           {"Day length", std::to_string(minutes / 60) + "h " +
                              std::to_string(minutes % 60) + "m"}};
    } else
      r = {{"Sun times", "Data unavailable"}};
    auto &m = optional("moon");
    if (!m.empty()) {
      r.push_back({"Moon illuminated", fmt(num(get(m, "illumination"))) + "%"});
      for (auto &pair : std::vector<std::pair<std::string, std::string>>{
               {"Next moonrise", "rise"},
               {"Next moonset", "set"},
               {"Next full moon", "full"},
               {"Next new moon", "new"}})
        r.push_back({pair.first, str(m, pair.second, "Unavailable")});
    } else
      r.push_back({"Moon data", "Unavailable"});
    rows(r, 65, 300, 115, 32);
    footer("forecast", "Open-Meteo / Ephem");
  }
  void air() {
    header(5, "Air Quality", "Local Conditions");
    auto &a = optional("air_quality");
    std::vector<std::pair<std::string, std::string>> r;
    if (!std::isfinite(num(get(a, "aqi"))))
      r = {{"Air quality", "Data unavailable"}};
    else {
      r = {{"US AQI", fmt(num(get(a, "aqi")))},
           {"Category", str(a, "category", "Unavailable")}};
      for (auto &p : std::vector<std::pair<std::string, std::string>>{
               {"PM2.5", "pm25"}, {"PM10", "pm10"}, {"Ozone", "o3"}}) {
        auto n = num(get(a, p.second));
        r.push_back(
            {p.first, std::isfinite(n) ? fmt(n, 1) + " ug/m3" : "Unavailable"});
      }
    }
    rows(r, 70, 255, 135, 40);
    footer("air_quality", "Open-Meteo model");
  }
  void uv() {
    header(5, "UV Index", "Forecast");
    txt("DATE", 65, 120, "normal", yellow);
    txt("MAX UV", 285, 120, "normal", yellow);
    txt("LEVEL", 425, 120, "normal", yellow);
    auto &daily = get(optional("forecast"), "daily");
    if (daily.empty())
      unavailable("UV forecast unavailable");
    for (size_t i = 0; i < daily.size() && i < 7; i++) {
      double v = num(get(daily[i], "uv_index"));
      txt(isofmt(str(daily[i], "date"), "%a %m/%d"), 65, 160 + i * 30);
      txt(fmt(v, 1), 305, 160 + i * 30);
      txt(!std::isfinite(v) ? "Unavailable"
          : v < 3           ? "Low"
          : v < 6           ? "Moderate"
          : v < 8           ? "High"
          : v < 11          ? "Very High"
                            : "Extreme",
          425, 160 + i * 30);
    }
    footer("forecast", "Open-Meteo forecast");
  }
  void radarPage() {
    header(6, "Live", "Radar");
    radar.clear();
    for (auto &p : get(get(data, "radar"), "frames")) {
      if (!p.is_string() || radar.size() >= 20)
        continue;
      auto img = image(p.get<std::string>(), 500, 300);
      if (img)
        radar.push_back(img);
    }
    rect(70, 100, 500, 300, {0, 20, 40, 255});
    if (radar.empty())
      unavailable("Radar unavailable");
    txt(str(get(data, "radar"), "source", "Radar imagery"), 70, 405, "tiny");
    centered(upper(str(get(data, "location"), "city") + ", " +
                   str(get(data, "location"), "state")),
             320, 420, "normal", yellow);
  }
  void historyPage(int which) {
    header(5, "30-Day", which ? "Precipitation" : "Temperature");
    txt("DATE", 60, 120, "normal", yellow);
    txt(which ? "AMOUNT" : "HIGH", which ? 300 : 320, 120, "normal", yellow);
    txt(which ? "STATUS" : "LOW", 480, 120, "normal", yellow);
    rect(50, 155, 540, 1, yellow);
    history[which].clear();
    for (auto &r : get(get(data, "history_data"),
                       which ? "precipitation" : "temperature")) {
      if (!r.is_array() || r.size() < size_t(which ? 2 : 3) ||
          !r[0].is_string())
        continue;
      std::array<std::string, 3> row;
      row[0] = isofmt(r[0], "%a %m/%d");
      double v = num(r[1]);
      if (which) {
        row[1] = !std::isfinite(v) ? "N/A"
                 : v == 0          ? "Trace"
                                   : fmt(v, 2) + "\"";
        row[2] = !std::isfinite(v) ? "Unavailable"
                 : v == 0          ? "Dry"
                 : v < .1          ? "Light"
                 : v < .5          ? "Moderate"
                                   : "Heavy";
      } else {
        row[1] = integer(v) + "°";
        row[2] = integer(num(r[2])) + "°";
      }
      history[which].push_back(row);
      if (history[which].size() == 31)
        break;
    }
    if (history[which].empty())
      unavailable("History unavailable");
  }
  void news() {
    header(1, "Local News", str(get(data, "news"), "source", "Local"));
    newsLines.clear();
    int count = 0;
    for (auto &h : get(get(data, "news"), "headlines")) {
      if (++count > 20)
        break;
      std::string s = h.is_array() && h.size() && h[0].is_string()
                          ? h[0].get<std::string>()
                      : h.is_string() ? h.get<std::string>()
                                      : "";
      auto lines = wrap(s, 512, "normal");
      newsLines.insert(newsLines.end(), lines.begin(), lines.end());
      newsLines.push_back("");
    }
    if (newsLines.empty())
      unavailable("Headlines unavailable");
  }
  void traffic() {
    const auto &feed = get(data, "traffic");
    header(1, "Local Traffic", str(feed, "source", "Local Traffic"));
    trafficLines.clear();
    const auto &items = get(feed, "items");
    if (!items.is_array()) {
      unavailable("Traffic unavailable");
      return;
    }
    if (items.empty())
      unavailable("No items in this feed");
    for (const auto &item : items) {
      for (const auto &field :
           {std::string("title"), std::string("description")}) {
        for (const auto &line : wrap(str(item, field), 512, "normal"))
          trafficLines.push_back({line, field == "title"});
      }
      trafficLines.push_back({"", false});
    }
    const double updated = num(get(feed, "updated_at"));
    txt(std::isfinite(updated)
            ? "Provider updated " +
                  datefmt(static_cast<time_t>(updated), "%b %d %I:%M %p")
            : "Provider update time unavailable",
        64, 405, "tiny");
    if (!str(get(data, "errors"), "traffic").empty())
      txt("Refresh failed - showing last received feed", 64, 382, "tiny",
          yellow);
  }
  void temperatureGraph() {
    header(5, "Temperature", "Forecast");
    struct DailyRange {
      double high = NAN;
      double low = NAN;
    };
    std::map<std::string, DailyRange> days;
    for (const auto &period : get(weather("forecast"), "periods")) {
      const std::string date = str(period, "startTime").substr(0, 10);
      if (date.size() != 10)
        continue;
      auto &day = days[date];
      double value = num(get(period, "temperature"));
      if (str(period, "temperatureUnit", "F") == "C")
        value = value * 1.8 + 32;
      if (!std::isfinite(value) || !get(period, "isDaytime").is_boolean())
        continue;
      double &bound = get(period, "isDaytime") == true ? day.high : day.low;
      if (!std::isfinite(bound))
        bound = value;
      else
        bound = get(period, "isDaytime") == true ? std::max(bound, value)
                                                 : std::min(bound, value);
    }
    if (days.empty()) {
      unavailable("Temperature forecast unavailable");
      return;
    }
    struct Column {
      std::string date;
      DailyRange range;
    };
    std::vector<Column> columns;
    auto date = iso(days.begin()->first);
    date.tm_hour = 12;
    for (int n = 0; n < 7; n++) {
      const auto stamp = std::mktime(&date);
      const auto key = datefmt(stamp, "%Y-%m-%d");
      if (key > days.rbegin()->first)
        break;
      auto found = days.find(key);
      columns.push_back(
          {key, found == days.end() ? DailyRange{} : found->second});
      date.tm_mday++;
    }
    double minimum = INFINITY, maximum = -INFINITY;
    for (const auto &column : columns)
      for (double value : {column.range.high, column.range.low})
        if (std::isfinite(value)) {
          minimum = std::min(minimum, value);
          maximum = std::max(maximum, value);
        }
    centered("HIGH °F", 220, 120, "normal", yellow);
    centered("LOW °F", 420, 120, "normal", cyan);
    if (std::isfinite(minimum)) {
      minimum = std::floor(minimum / 5) * 5 - 5;
      maximum = std::ceil(maximum / 5) * 5 + 5;
      for (int tick = 0; tick < 5; tick++) {
        int y = 160 + tick * 35;
        rect(80, y, 490, 1, {80, 100, 160, 255});
        txt(fmt(maximum - (maximum - minimum) * tick / 4), 70, y - 8, "tiny",
            white, 2);
      }
    } else
      centered("Temperature values unavailable", 320, 250);
    for (size_t i = 0; i < columns.size(); i++) {
      const int x = 100 + int(i) * 75;
      auto &column = columns[i];
      auto yFor = [&](double value) {
        return 300 - int((value - minimum) * 140 / (maximum - minimum));
      };
      const double high = column.range.high, low = column.range.low;
      if (std::isfinite(high) && std::isfinite(low)) {
        int top = yFor(std::max(high, low)), bottom = yFor(std::min(high, low));
        rect(x - 2, top, 4, std::max(1, bottom - top), white);
      }
      if (std::isfinite(high))
        rect(x - 9, yFor(high) - 2, 18, 4, yellow);
      if (std::isfinite(low))
        rect(x - 9, yFor(low) - 2, 18, 4, cyan);
      centered(isofmt(column.date, "%a"), x, 326, "tiny");
      centered(isofmt(column.date, "%m/%d"), x, 344, "tiny");
      centered("H " + fmt(high), x, 363, "tiny", yellow);
      centered("L " + fmt(low), x, 381, "tiny", cyan);
    }
  }
  void rebuild() {
    images.clear();
    textCache.clear();
    textBytes = 0;
    ticker.clear();
    ticker.push_back(" +++ " +
                     upper(str(get(data, "location"), "city") + ", " +
                           str(get(data, "location"), "state")) +
                     " +++ ");
    const auto &c = weather("current");
    auto observation = [&](const std::string &key) {
      return num(get(get(c, key), "value"));
    };
    std::string conditions =
        "CURRENTLY: " + fmt(observation("temperature") * 1.8 + 32) + "°F, " +
        str(c, "textDescription", "Unavailable");
    if (std::isfinite(observation("relativeHumidity")))
      conditions +=
          " ... HUMIDITY: " + fmt(observation("relativeHumidity")) + "%";
    if (std::isfinite(observation("windSpeed")))
      conditions += " ... WIND: " + compass(observation("windDirection")) +
                    " " + fmt(windMph(get(c, "windSpeed"))) + " MPH";
    ticker.push_back(conditions);
    auto &p = get(weather("forecast"), "periods");
    for (size_t i = 0; i < p.size() && i < 3; i++) {
      const auto &period = p[i];
      bool day = get(period, "isDaytime") != false;
      if ((i == 1 && day) || (i == 2 && !day))
        continue;
      std::string item = " +++ " + upper(str(period, "name")) + ": " +
                         upper(str(period, "shortForecast"));
      item += day ? ", HIGH " : ", LOW ";
      item += fmt(num(get(period, "temperature"))) + "°F";
      if (i == 0 && day && p.size() > 1 && get(p[1], "isDaytime") == false)
        item += " LOW " + fmt(num(get(p[1], "temperature"))) + "°F";
      ticker.push_back(item + " +++ ");
    }
    ticker.push_back(
        " +++ VISIT WEATHER.GOV FOR THE LATEST WEATHER INFORMATION +++ ");
    for (building = 0; building < 14; building++) {
      if (std::find(enabledPages.begin(), enabledPages.end(), building) ==
          enabledPages.end())
        continue;
      pages[building] = surface();
      target = pages[building];
      placements[building].clear();
      switch (building) {
      case 0:
        current();
        break;
      case 1:
        local();
        break;
      case 2:
        extended();
        break;
      case 3:
        hourly();
        break;
      case 4:
        weekend();
        break;
      case 5:
        sunmoon();
        break;
      case 6:
        air();
        break;
      case 7:
        uv();
        break;
      case 8:
        radarPage();
        break;
      case 9:
        historyPage(0);
        break;
      case 10:
        historyPage(1);
        break;
      case 11:
        news();
        break;
      case 12:
        temperatureGraph();
        break;
      case 13:
        traffic();
        break;
      }
    }
  }
  void drawIcon(const Placement &p) {
    std::string key =
        p.name + ":" + std::to_string(p.w) + ":" + std::to_string(p.h);
    if (!icons.count(key)) {
      Icon icon;
      auto *a =
          IMG_LoadAnimation((assets + "/icons/" + p.name + ".gif").c_str());
      if (a) {
        double scale = std::min(double(p.w) / a->w, double(p.h) / a->h);
        int w = std::max(1, int(a->w * scale)),
            h = std::max(1, int(a->h * scale));
        if (p.w == 86 && p.h == 75) {
          w = p.w;
          h = p.h;
        }
        for (int i = 0; i < a->count; i++) {
          auto s = surface(w, h);
          SDL_FillRect(s.get(), nullptr, 0);
          auto decoded = own(SDL_ConvertSurfaceFormat(
              a->frames[i], SDL_PIXELFORMAT_ARGB8888, 0));
          if (SDL_BlitScaled(decoded.get(), nullptr, s.get(), nullptr) != 0)
            throw std::runtime_error("Icon scaling failed for " + p.name +
                                     ": " + SDL_GetError());
          icon.frames.push_back(s);
          int delay = std::max(20, a->delays[i]);
          icon.delays.push_back(delay);
          icon.duration += delay;
        }
        IMG_FreeAnimation(a);
      } else
        std::cerr << "Animated icon unavailable: " << p.name << " "
                  << IMG_GetError() << "\n";
      size_t bytes = 0;
      for (auto &f : icon.frames)
        bytes += f->pitch * f->h;
      if (icons.size() >= 64 || iconBytes + bytes > 64 * 1024 * 1024) {
        icons.clear();
        iconBytes = 0;
      }
      iconBytes += bytes;
      icons.emplace(key, std::move(icon));
    }
    auto &i = icons.at(key);
    if (i.frames.empty())
      return;
    int t = int(elapsed * 1000) % i.duration;
    size_t n = 0;
    while (n + 1 < i.frames.size() && t >= i.delays[n]) {
      t -= i.delays[n];
      n++;
    }
    blit(i.frames[n], p.x - i.frames[n]->w / 2, p.y - i.frames[n]->h / 2);
  }

public:
  Renderer(std::string a, std::string s, const std::vector<int> &selected)
      : assets(a), snapshot(s), enabledPages(selected) {
    if (SDL_Init(0) || TTF_Init())
      throw std::runtime_error(SDL_GetError());
    IMG_Init(IMG_INIT_PNG);
    struct FontSpec {
      const char *key;
      const char *file;
      int size;
    };
    for (auto f :
         std::vector<FontSpec>{{"title", "star4000", 32},
                               {"normal", "star4000", 20},
                               {"large", "star4000_large", 32},
                               {"extended", "star4000_extended", 32},
                               {"small", "star4000_small", 28},
                               {"forecast", "star4000_small", 24},
                               {"tiny", "star4000", 16},
                               {"scroller", "star4000_extended", 24}}) {
      auto *p = TTF_OpenFont((assets + "/fonts_ttf/" + f.file + ".ttf").c_str(),
                             f.size);
      if (!p)
        throw std::runtime_error("Font unavailable: " +
                                 std::string(TTF_GetError()));
      fonts[f.key] = p;
    }
    frame = surface();
    reload();
    if (!loaded)
      rebuild();
  }
  ~Renderer() {
    for (auto &[k, f] : fonts)
      TTF_CloseFont(f);
  }
  void reload() {
    try {
      auto m = std::filesystem::last_write_time(snapshot);
      if (loaded && m == modified)
        return;
      std::ifstream in(snapshot);
      J next;
      in >> next;
      if (!next.is_object() || next.value("version", 0) != 1)
        throw std::runtime_error("Unsupported snapshot schema");
      data = std::move(next);
      modified = m;
      loaded = true;
      rebuild();
      std::cerr << "Renderer loaded snapshot\n";
    } catch (const std::exception &e) {
      std::cerr << "Renderer snapshot unavailable; retaining prior data: "
                << e.what() << "\n";
    }
  }
  SDL_Surface *draw(int page, double seconds, double rotationSeconds = 180.0) {
    elapsed = seconds;
    target = frame;
    blit(pages[page], 0, 0);
    for (auto &p : placements[page])
      drawIcon(p);
    double phase =
        std::floor(seconds / rotationSeconds) * 15.0 + std::fmod(seconds, 15.0);
    if (page == 3 && !hourLines.empty()) {
      SDL_Rect clip{0, 155, 640, 265};
      SDL_SetClipRect(target.get(), &clip);
      int height = hourLines.size() * 25,
          base = 420 - int(std::fmod(seconds * 20, height + 265));
      for (int loop = 0; loop < 2; loop++)
        for (size_t i = 0; i < hourLines.size(); i++) {
          int y = base + loop * height + i * 25;
          if (y > 125 && y < 440)
            txt(hourLines[i], 65, y);
        }
      SDL_SetClipRect(target.get(), nullptr);
    }
    if (page == 8) {
      if (!radar.empty()) {
        int i = int(seconds * 2) % radar.size();
        blit(radar[i], 70, 100);
        txt("Frame " + std::to_string(i + 1) + "/" +
                std::to_string(radar.size()),
            490, 380, "tiny");
      }
      rect(70, 100, 500, 2, yellow);
      rect(70, 398, 500, 2, yellow);
      rect(70, 100, 2, 300, yellow);
      rect(568, 100, 2, 300, yellow);
      std::array<SDL_Color, 4> colors{{{0, 100, 0, 255},
                                       {255, 255, 0, 255},
                                       {255, 140, 0, 255},
                                       {255, 0, 0, 255}}};
      const char *labels[] = {"Light", "Moderate", "Heavy", "Intense"};
      for (int n = 0; n < 4; n++) {
        rect(80, 110 + n * 20, 15, 15, white);
        rect(81, 111 + n * 20, 13, 13, colors[n]);
        txt(labels[n], 100, 110 + n * 20, "tiny");
      }
    }
    if (page == 9 || page == 10) {
      int which = page - 9;
      auto &h = history[which];
      int starts = std::max(1, int(h.size()) - 7),
          start = int(std::max(0.0, phase - 3) * 10 / 30) % starts;
      for (int i = 0; i < 8 && start + i < int(h.size()); i++) {
        txt(h[start + i][0], 60, 160 + i * 30);
        txt(h[start + i][1], which ? 310 : 330, 160 + i * 30);
        txt(h[start + i][2], 490, 160 + i * 30);
      }
    }
    if (page == 11) {
      SDL_Rect clip{64, 105, 512, 280};
      SDL_SetClipRect(target.get(), &clip);
      int step = TTF_FontLineSkip(font("normal")) + 3,
          maxscroll = std::max(0, int(newsLines.size()) * step - 280),
          offset = maxscroll ? int(phase * 20) % (maxscroll + 60) : 0;
      for (size_t i = 0; i < newsLines.size(); i++) {
        int y = 105 + i * step - offset;
        if (y > 105 - step && y < 385)
          txt(newsLines[i], 64, y);
      }
      SDL_SetClipRect(target.get(), nullptr);
    }
    if (page == 13) {
      SDL_Rect clip{64, 105, 512, 275};
      SDL_SetClipRect(target.get(), &clip);
      const int step = TTF_FontLineSkip(font("normal")) + 3;
      const int maxScroll =
          std::max(0, int(trafficLines.size()) * step - clip.h);
      const int offset = maxScroll ? int(phase * 20) % (maxScroll + 60) : 0;
      for (size_t index = 0; index < trafficLines.size(); ++index) {
        const int y = clip.y + int(index) * step - offset;
        if (y > clip.y - step && y < clip.y + clip.h)
          txt(trafficLines[index].first, clip.x, y, "normal",
              trafficLines[index].second ? yellow : white);
      }
      SDL_SetClipRect(target.get(), nullptr);
    }
    auto now = time(nullptr);
    auto clock = datefmt(now, "%I:%M %p");
    if (clock[0] == '0')
      clock.erase(0, 1);
    txt(clock, 590, 34, "small", white, 2);
    txt(upper(datefmt(now, "%a %b %d")), 590, 54, "small", white, 2);
    rect(0, 430, 640, 30, {0, 0, 80, 255});
    double distance = seconds * 100;
    for (auto &s : ticker) {
      auto p = text(s, "scroller", white);
      double span = 640 + p->w;
      if (distance < span) {
        SDL_Rect clip{0, 430, 640, 30};
        SDL_SetClipRect(target.get(), &clip);
        blit(p, 640 - int(distance), 435);
        SDL_SetClipRect(target.get(), nullptr);
        return frame.get();
      }
      distance -= span;
    }
    double total = 0;
    for (auto &s : ticker)
      total += 640 + text(s, "scroller", white)->w;
    if (total > 0) {
      distance = std::fmod(seconds * 100, total);
      for (auto &s : ticker) {
        auto p = text(s, "scroller", white);
        if (distance < 640 + p->w) {
          SDL_Rect clip{0, 430, 640, 30};
          SDL_SetClipRect(target.get(), &clip);
          blit(p, 640 - int(distance), 435);
          SDL_SetClipRect(target.get(), nullptr);
          break;
        }
        distance -= 640 + p->w;
      }
    }
    return frame.get();
  }
};
int main(int argc, char **argv) {
  try {
    std::string assets = "/app/weatherstar_assets",
                snapshot = "/data/snapshot.json", dump;
    double duration = 0;
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (i + 1 >= argc)
        throw std::runtime_error("Missing argument for " + a);
      std::string v = argv[++i];
      if (a == "--assets")
        assets = v;
      else if (a == "--snapshot")
        snapshot = v;
      else if (a == "--dump-all")
        dump = v;
      else if (a == "--duration")
        duration = std::stod(v);
      else
        throw std::runtime_error("Unknown option " + a);
    }
    const auto selectedPages = weatherstar::selected_pages();
    std::vector<int> rotation;
    for (const auto &name : selectedPages)
      rotation.push_back(weatherstar::page_index(name));
    auto preparedPages = rotation;
    if (!dump.empty()) {
      preparedPages.clear();
      for (size_t index = 0; index < weatherstar::page_names.size(); ++index)
        preparedPages.push_back(static_cast<int>(index));
    }
    Renderer r(assets, snapshot, preparedPages);
    if (!dump.empty()) {
      std::filesystem::create_directories(dump);
      for (int p = 0; p < 14; p++) {
        auto path =
            dump + "/page-" + (p < 10 ? "0" : "") + std::to_string(p) + ".bmp";
        if (SDL_SaveBMP(r.draw(p, p * 15 + 5), path.c_str()))
          throw std::runtime_error(SDL_GetError());
      }
      return 0;
    }
    auto start = std::chrono::steady_clock::now();
    std::vector<unsigned char> rgb(640 * 480 * 3);
    std::setvbuf(stdout, nullptr, _IOFBF, rgb.size());
    for (uint64_t count = 0;; count++) {
      double t = count / 30.0;
      if (duration > 0 && t >= duration)
        break;
      if (count && count % 30 == 0)
        r.reload();
      auto *f = r.draw(rotation[static_cast<size_t>(t / 15) % rotation.size()],
                       t, rotation.size() * 15.0);
      if (SDL_ConvertPixels(640, 480, f->format->format, f->pixels, f->pitch,
                            SDL_PIXELFORMAT_RGB24, rgb.data(), 640 * 3))
        throw std::runtime_error(SDL_GetError());
      if (std::fwrite(rgb.data(), 1, rgb.size(), stdout) != rgb.size())
        return 0;
      std::this_thread::sleep_until(
          start + std::chrono::microseconds((count + 1) * 1000000 / 30));
    }
    std::fflush(stdout);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "weather-render: " << e.what() << "\n";
    return 1;
  }
}
