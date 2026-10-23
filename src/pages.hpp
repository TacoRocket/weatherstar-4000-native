#pragma once

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace weatherstar {
inline constexpr std::array<const char *, 16> page_names{
    "current",
    "local",
    "extended",
    "hourly",
    "weekend",
    "sunmoon",
    "aqi",
    "uv",
    "radar",
    "temperature-history",
    "precipitation-history",
    "news",
    "temperature-graph",
    "traffic",
    "traffic-flow",
    "traffic-bottlenecks"};

inline int page_index(const std::string &name) {
  auto found = std::find(page_names.begin(), page_names.end(), name);
  if (found == page_names.end())
    throw std::runtime_error("Unknown WEATHERSTAR_PAGES page '" + name + "'");
  return static_cast<int>(found - page_names.begin());
}

inline bool page_enabled(const std::vector<std::string> &pages,
                         const std::string &name) {
  return std::find(pages.begin(), pages.end(), name) != pages.end();
}

inline std::vector<std::string> selected_pages() {
  const char *configured = std::getenv("WEATHERSTAR_PAGES");
  if (!configured)
    return {page_names.begin(), page_names.begin() + 12};

  const std::string value(configured);
  std::vector<std::string> selected;
  size_t start = 0;
  while (true) {
    const size_t comma = value.find(',', start);
    std::string name = value.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    const size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
      throw std::runtime_error("WEATHERSTAR_PAGES contains an empty page name");
    const size_t last = name.find_last_not_of(" \t\r\n");
    name = name.substr(first, last - first + 1);
    page_index(name);
    if (page_enabled(selected, name))
      throw std::runtime_error("WEATHERSTAR_PAGES contains duplicate page '" +
                               name + "'");
    selected.push_back(name);
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return selected;
}
} // namespace weatherstar
