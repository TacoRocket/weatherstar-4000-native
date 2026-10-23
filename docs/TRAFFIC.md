# Optional traffic pages

Traffic is disabled by default. Add any of `traffic`, `traffic-flow`, and
`traffic-bottlenecks` to `WEATHERSTAR_PAGES` alongside the weather pages you want.
The `traffic` page uses the separately configured RSS incident feed. RSS does not
provide the measured road speeds used by the two map pages.

For the included Houston map, set:

```dotenv
WEATHERSTAR_TRAFFIC_MAP_CONFIG=/app/config/traffic/houston.json
```

For your own layout, copy `config/traffic/houston.json`, edit it, mount it into the
container, and point that variable at its container path. For example, add this read-only mount under the existing service volumes:

```yaml
      - ./my-traffic.json:/app/config/traffic/custom.json:ro
```

Then set `WEATHERSTAR_TRAFFIC_MAP_CONFIG=/app/config/traffic/custom.json` in `.env`. Keep `title` short
(roughly 20 characters) for the television header. The included example describes
regional roads and landmarks; it contains no household location or ZIP code.

The only implemented speed provider is **Houston TranStar**. A new layout using
its roads needs only configuration. A different city's feed requires a provider
adapter in `src/traffic_map.py`; changing the title or RSS URL does not implement
one. No registration, API key, or additional service is required for TranStar.

## Map configuration

The JSON file owns the map title, geography, drawn roads, and labels:

| Field | Meaning |
| --- | --- |
| `title` | Header title, such as `Houston Traffic`. |
| `provider` | Must be `transtar`. |
| `viewport` | `[west, south, east, north]` geographic bounds in degrees. |
| `roads` | Ordered array of road groups. Later roads draw over earlier roads. |
| `labels` | Array of `{ "point": [x, y], "text": "label" }`. |

Each road contains:

- `key`: unique group name.
- `routes`: exact provider route names, such as `IH-10 Katy`.
- `dirs`: an opposing direction pair (`EB/WB` or `NB/SB`), in drawing order.
- `points`: the road's polyline as `[x, y]` pairs. Coordinates are final pixels in
  a 512 by 240 road area. The full image is 512 by 290; its lower strip is reserved
  for the legend and source time. At least two distinct points are required.
- `clip_to_viewport`: whether to select only whole provider segments with at least
  one geometry vertex inside the geographic viewport. Despite the field name,
  this does **not** clip segment geometry, split travel times, or interpolate speed.
  The Houston example enables it for radial freeways and leaves it off for named
  loop sides.

For example, moving a label changes only its drawing position:

```json
{ "point": [74.56, 57], "text": "Cypress" }
```

The two directions are drawn as parallel lines with small arrows. The common
renderer owns their colors, legend, and source timestamp. Those presentation
rules stay consistent between Flow and Bottlenecks.

## What the colors mean

`src/traffic_map.py` separates the TranStar parser, road aggregation, and Pillow
schematic renderer. The parser reads JSON-compatible arguments from the provider's
JavaScript data file; it never executes JavaScript.

Only main-lane records are selected. HOV and managed lanes are excluded. Each
configured road and direction is one aggregate:

- **Flow:** total measured miles divided by total measured travel time, expressed
  in MPH. This is a travel-time-weighted result, not an arithmetic mean of speeds.
- **Bottlenecks:** the lowest reported speed among the selected measured segments.
- **Gray:** no matching records, any selected record missing usable speed/travel
  time, or stale/unknown source time. A partial group does not appear green just
  because its available readings are fast.

The speed bands are below 20, 20–29, 30–39, 40–49, and 50+ MPH. Roads are schematic;
colors describe the entire configured group, not separately measured stretches
between every drawn junction. The Houston example has 42 directional groups and
intentionally has no southern SH-99 loop section.

## Refresh and failure behavior

The collector refreshes maps every five minutes when either map page is enabled.
The helper exits after drawing; video playback reads cached PNG files. With both
map pages disabled, the collector does not fetch or render the map.

Source age comes from the provider's `Last-Modified` header. A missing/invalid
header, an age over 15 minutes, or a timestamp more than five minutes in the future
makes measurements stale. The page renderer checks age again during playback and
shows unavailable data rather than continuing to display an old colored map.
Fetch, configuration, and parsing errors are visible in collector errors. A
changed configuration invalidates the cached map before it can be reused.

The helper publishes each pair of PNGs plus metadata into a generation directory
and atomically switches a `current` symlink. Metadata points to immutable images,
so a reader cannot mix one refresh's Flow image with another refresh's metadata.
The most recent three generations remain for readers holding an earlier snapshot.
Generated snapshots and provider records belong in runtime storage, not Git.

To exercise the helper directly (Python 3 and Pillow are required):

```sh
python3 src/traffic_map.py \
  --config config/traffic/houston.json \
  --output-dir /tmp/weatherstar-traffic
```

`metadata.json` contains `title`, `source`, `feed_url`, `source_updated_at`,
`fetched_at`, `stale`, `stale_after_seconds`, `viewport`, `provider_rows`, `scope`,
`groups`, `flow_image`, and `bottlenecks_image`. Each group records its key,
direction, routes, coverage counts/distances, completeness, both aggregate speeds,
and the provider records used. The main collector embeds this object under
`traffic_map`; the PNG paths are local container paths, not public URLs.
