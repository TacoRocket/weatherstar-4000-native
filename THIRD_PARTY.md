# Third-party material and data

## WeatherStar reference

Renderer layout, behavior and bundled backgrounds, icons, fonts and logos derive from [wesellis/weatherstar-4000](https://github.com/wesellis/weatherstar-4000), including the repaired reference branch at [TacoRocket/weatherstar-4000](https://github.com/TacoRocket/weatherstar-4000/tree/headless-display-repairs).

The copied upstream license is reproduced in full in [LICENSE](LICENSE): **MIT License; Copyright (c) 2024 WeatherStar 4000 Python Contributors.** Keep that notice with substantial copies of the software and associated material. This identifies the upstream repository's license; it does not claim ownership of third-party trademarks or endorsement by their owners.

No reference music is copied into this repository or its image. The source-code license is not used as evidence of music rights. Optional user-provided audio remains the user's separately supplied material.

## Runtime and build dependencies

The image installs Debian packages for SDL2, SDL2_image, SDL2_ttf, FFmpeg, libcurl, libxml2, cpp-httplib, nlohmann-json, Python, Ephem and Pillow, with their transitive dependencies. These components retain their own licenses; the project's MIT license does not replace them. Debian package copyright/license records are available under `/usr/share/doc/<package>/copyright` in the relevant image stage. CMake and the C++ toolchain are build-stage dependencies.

## External data

| Provider | Use |
|---|---|
|[NOAA / National Weather Service](https://www.weather.gov/documentation/services-web-api)|US point discovery, current observations and forecasts|
|[NOAA radar](https://radar.weather.gov/)|Regional radar imagery|
|[Open-Meteo](https://open-meteo.com/)|Daily sun/UV forecasts, air-quality estimates and recent historical weather|
|[Ephem](https://rhodesmill.org/pyephem/)|Local astronomical calculations|
|Configured RSS publishers|Dated news or traffic items; source labels and original timestamps are retained|

Provider data is not licensed by this repository's MIT notice. Follow each provider's current terms, attribution and service limits when configuring a deployment. An HTTP success does not guarantee data accuracy or timeliness. No provider endorsement is claimed.

For a configured Houston TranStar RSS feed, see the [official RSS directory](https://traffic.houstontranstar.org/rss/rss_info.aspx), [FAQ usage guidance](https://www.houstontranstar.org/faq/webfaq.aspx) and [content terms](https://traffic.houstontranstar.org/disclaimer.aspx). Public code supports generic RSS text; it does not bundle TranStar map data, camera imagery or Esri tiles.
