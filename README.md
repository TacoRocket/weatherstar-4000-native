# WeatherStar 4000 Native

A retro weather channel for your own screen: animated weather icons, forecasts, radar, scrolling history and RSS headlines, delivered as a 640×480 HLS stream at 30 fps.

The collector keeps weather data ready. Rendering and encoding start when a viewer requests HLS and stop after 120 seconds without playback requests. Multiple viewers share one producer.

**Publication status:** the GHCR workflow is configured; the image instructions below become usable after its first successful publication. Current public-build playback and resource validation are pending. Historical twelve-page results are documented separately in [Benchmarks](docs/BENCHMARKS.md).

## On screen

![Current conditions](docs/images/current.png)

![Temperature forecast](docs/images/temperature-graph.png)

![Configured traffic RSS](docs/images/traffic-rss.png)

Screenshots use Washington weather and a separately configured Houston RSS feed. Traffic geography follows the feed URL.

## Quick start

Requires Docker Compose on `linux/amd64`. ARM images are not currently built or claimed as tested.

```sh
git clone https://github.com/TacoRocket/weatherstar-4000-native.git
cd weatherstar-4000-native
cp .env.example .env
```

Edit `.env` with your latitude, longitude, location label and time zone. Set `WEATHERSTAR_PUBLIC_URL` to the HTTP address your viewers can reach, including the published port; `localhost` works only on the server itself. NOAA coverage limits the current application to supported US locations.

Once the image is published:

```sh
docker pull ghcr.io/tacorocket/weatherstar-4000-native:latest
docker compose up -d
```

The Compose file persists collected data in a named volume and keeps HLS segments in tmpfs. The container runs as UID/GID 1001. To build from source instead, use `docker compose -f compose.yaml -f compose.build.yaml up -d --build`.

| URL on your configured server | Purpose |
|---|---|
|`/weather.m3u`|Channel playlist for an IPTV player|
|`/hls/stream.m3u8`|Direct HLS playback; starts the producer on demand|
|`/health`|JSON producer status; does not start playback|

The HTTP service has no authentication or TLS. The supplied configuration is for a trusted LAN; any external access needs a separately configured access boundary. First tune-in waits for fresh segments and can return a retryable response while starting. Health status is not a guarantee that every upstream provider is current.

## Choose your pages

`WEATHERSTAR_PAGES` is an ordered, comma-separated list. Each page lasts 15 seconds. The default is the first twelve below; add `temperature-graph` and/or `traffic` to enable the additional pages. Empty, duplicate and unknown page names are rejected.

| Name | Page |
|---|---|
|`current`|Current conditions|
|`local`|Detailed local forecast|
|`extended`|Extended forecast with animated icons|
|`hourly`|Scrolling hourly forecast|
|`weekend`|Saturday and Sunday forecasts|
|`sunmoon`|Sun times and moon events|
|`aqi`|Air quality and pollutants|
|`uv`|Daily UV forecast|
|`radar`|Animated regional radar|
|`temperature-history`|Thirty-day temperature history|
|`precipitation-history`|Thirty-day precipitation history|
|`news`|Dated publisher RSS headlines|
|`temperature-graph`|Forecast high/low graph using actual dated periods|
|`traffic`|Configured traffic RSS text, preserving status and timestamps|

Set `WEATHERSTAR_NEWS_RSS_URL` and `WEATHERSTAR_NEWS_SOURCE` for news; an empty URL leaves that page unavailable. Traffic uses `WEATHERSTAR_TRAFFIC_RSS_URL` and `WEATHERSTAR_TRAFFIC_SOURCE`. Its scope is the configured feed, independent of weather coordinates. It is a text page, not a congestion map. No synthetic news or traffic measurements are supplied.

Removing optional pages stops their associated collection when no remaining page needs that source. Sun & Moon and UV share one daily forecast request; both history pages share one historical request. Core NOAA observation/forecast/hourly collection continues independently.

## Data and refreshes

| Data | Source | Collection interval |
|---|---|---|
|Observations and forecasts|NOAA / National Weather Service|5 minutes|
|Radar imagery|NOAA|5 minutes when enabled|
|Sun times and UV|Open-Meteo|30 minutes when enabled|
|AQI and pollutants|Open-Meteo|30 minutes when enabled|
|Moon events|Local Ephem calculation|Once per local day when enabled|
|Historical temperatures and rain|Open-Meteo|1 hour when enabled|
|News|Your configured RSS publisher|15 minutes when enabled|
|Traffic|Your configured RSS provider|5 minutes when enabled|

Intervals are minimum refresh spacing, checked by the collector loop. Failed refreshes retain prior values and original source timestamps, with errors recorded in the snapshot. Collection time does not make an old observation current. Provider coverage and availability vary; this is not an official warning service. Source links and dependency notices are in [Third-party material](THIRD_PARTY.md).

## Audio

The default stream contains silent AAC audio. No music is bundled. To use your own audio, add a read-only file mount to Compose and set `WEATHERSTAR_MUSIC_FILE` to its absolute path **inside the container**. The file loops through FFmpeg; use audio you have permission to play.

## How it works

```text
Providers → C++ collector → atomic JSON snapshot + radar files
                                      ↓
HLS request → C++ SDL renderer → raw RGB frames → FFmpeg → HLS
```

The renderer caches artwork and text, performs no HTTP requests and requires no X server. The C++ HTTP service shares one renderer/encoder among viewers and enforces the idle timeout. Short Python helpers use Ephem for astronomy and Pillow for radar processing; this is not a claim of an entirely C++ runtime.

The hosted workflow builds and publishes AMD64 images on `main`, version tags and manual runs. Pull requests build without publishing. See [Benchmarks](docs/BENCHMARKS.md) for measured limits and [Roadmap](docs/ROADMAP.md) for deferred pages.

Based on [WeatherStar 4000 Python](https://github.com/wesellis/weatherstar-4000), with its MIT notice retained in [LICENSE](LICENSE). This project is independent and does not claim endorsement by weather providers or brand owners.
