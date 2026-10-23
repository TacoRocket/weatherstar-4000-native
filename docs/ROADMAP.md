# Page roadmap

This is the active queue for additional pages. Existing reference code is not proof that a page has live data. A placeholder is complete only when its displayed values come from a defined source, collection happens outside rendering, and missing or stale data remains visible.

The twelve-page baseline already includes Weekend Forecast, Sun & Moon, Air Quality and UV Index. Those four pages are not future additions. Temperature and precipitation history and configured local RSS news are also in that baseline.

## Active: public traffic RSS and temperature graph

- **Generic traffic RSS:** the public page accepts a configured RSS source and renders its text, status and timestamps. Its coverage follows that feed, not the weather coordinates. Cleared incidents, inactive scheduled closures and failed retrievals must remain distinct. A Houston-specific congestion-map experiment is private and is not included in this repository or its public feature claims. Cameras and map-data redistribution are outside the public slice.
- **Seven-day temperature graph:** reuse the existing NOAA forecast. Pair actual daytime and nighttime values by date; never invent a missing high or low. This adds a presentation of existing data, not another weather provider.

Both page names are present in the current code and are opt-in after the default twelve pages. Current viewing validation, resource comparison and image publication are still pending; code presence is not their completion criterion. Report rendering cost separately from any new collection cost.

## Deferred: pages using existing data

| Page | Remaining work |
|---|---|
|Latest Observations|Show the selected station's measurements and observation time. This is one station, not a regional observations network. Much of its content duplicates Current Conditions.|
|Wind & Pressure|Use existing observation fields and units; preserve calm and north as valid values. Remove the reference's fabricated steady-pressure trend.|
|Almanac|Replace fixed sun/moon values with existing dated optional data. Assess whether it adds useful information beyond the current Sun & Moon page.|

## Deferred: pages requiring provider decisions

| Page | Required data boundary |
|---|---|
|Weather alerts and severe alerts|Collect actual authoritative alerts with location, validity and status. Forecast keywords and a missing response cannot establish an all-clear.|
|Earthquakes|Add cached USGS collection with an explicit time range and scope. The reference performs requests during drawing; that behavior must not carry over.|
|Marine and tides|Choose coastal locations and actual marine/tide products. The reference's waves, water temperature, risk and tide values are fixed examples.|
|Travel cities|Choose cities and collect their weather. The reference contains fixed temperatures and conditions.|
|Weather records|Choose a station-specific historical-record source. Thirty days of recent observations cannot establish all-time records.|
|Monthly outlook|Use a real regional outlook with issue and validity dates. The reference's probabilities and source label do not come from a request.|
|Markets|Choose and configure an appropriate market source, identify instruments correctly, and keep credentials outside the repository. ETF share prices must not be labeled as index levels.|
|Additional real news|Choose actual publisher feeds and retain dates and attribution. Reference MSN/Reddit pages use fabricated headlines; those pages are not live integrations. Existing configured local RSS remains separate.|

These deferred items are not part of the traffic/graph slice. Each needs its named source or product decision before implementation; no placeholder text should be promoted to a live-data claim.

## Container delivery

The hosted Container workflow builds `linux/amd64` with the repository Dockerfile. Pull requests build without publishing. Pushes to `main`, `v*` tags and manual runs can publish to `ghcr.io/tacorocket/weatherstar-4000-native` using the job's GitHub token. Main publishes `main` and `latest`; builds also receive a commit tag, and semantic version tags receive a version tag. Image availability depends on a successful hosted run and registry visibility. No ARM support is claimed by this workflow.

## Attribution

The renderer and artwork derive from [wesellis/weatherstar-4000](https://github.com/wesellis/weatherstar-4000) and its MIT-licensed WeatherStar 4000 Python work; retain the applicable license and notices with copied material. That attribution does not establish a license for bundled music. Do not distribute reference music based solely on the source-code MIT license.
