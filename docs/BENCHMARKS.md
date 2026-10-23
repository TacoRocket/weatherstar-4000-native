# Benchmark evidence and limits

These are historical results from an isolated twelve-page native prototype on October 22, 2026. They are **not measurements of the current configurable sixteen-page public build**, its silent-audio default or its traffic and graph additions.

Each resource window lasted about 302 seconds with 15-second samples and a concurrently measured Python reference. CPU is relative to one core; memory is mean container working set.

| Window | Python CPU | Native CPU | Python memory | Native memory |
|---|---:|---:|---:|---:|
|Cold idle, no viewers|43.77%|0.28%|202.95 MiB|14.38 MiB|
|Viewing|45.25%|21.46%|202.86 MiB|78.30 MiB|

The viewing prototype used about 53% less CPU and 61% less memory in this comparison. Peak working sets were 203.83 MiB and 86.37 MiB respectively. This is one environment and short run, not a hardware-independent performance guarantee.

Cold idle was measured before later icon/background and HTTP corrections, while neither renderer nor encoder was running. It does not establish immediate post-playback memory reclamation. After the idle timeout, a separate point sample showed 20.38 MiB and zero sampled CPU; that point is not another five-minute average. Build caches and image sizes are separate from runtime working set.

## What the prototype demonstrated

- Twelve pages were compared against the reference's complete rotation. Incorrect backgrounds and missing icons were corrected and rechecked.
- A 310-second H.264 640×480/30 fps and AAC recording decoded successfully. All 9,300 video packets advanced continuously at 1/30 second, without duplicate or backward timestamps.
- Selected frame pairs showed ticker movement, an animated Sunny icon, changing radar frames, scrolling hourly/history rows and later news headlines.
- Two clients shared one producer. Cold playlist readiness was 2.219 seconds; this measures playlist availability, not a TV player's first picture.
- The 120-second inactivity policy stopped renderer and encoder. A new request started a fresh producer generation and fresh segment names.
- Collection continued without a viewer and refreshed a snapshot during playback without a video timestamp gap.
- A separate network-disabled failure exercise preserved last-good data, original timestamps and radar files while recording errors.

The recorded prototype included externally supplied music. A short audio timestamp interval at its loop boundary also occurred in the reference; no listening assessment was performed. The public image bundles no music and defaults to silence.

## What remains unproven

The historical recording does not prove every weather icon, all thirty historical dates across repeated visits, overnight stability, current public-build performance or playback in a particular TV application. TV-app compatibility and perceived smoothness remain unverified. Additional pages and configurable source gating require their own current viewing/resource evidence.

Provider fetch success also does not establish observation freshness: the prototype retained an older NOAA observation supplied by its selected station. Failure retention and timestamps must not be described as live upstream measurements when the underlying report is old.

Exact behavior differs from the Python reference in documented ways: history advances more slowly between rotations; failed RSS/radar updates keep last-good content; missing historical precipitation remains unknown; wind units are respected; an unsupported pressure arrow is omitted; and radar attribution identifies NOAA. The comparison establishes the sampled twelve-page capability, not universal pixel equivalence.

Original raw recordings and environment details remain private. These aggregate results are included for context and are not a reproducibility package for the public revision. Current validation should record its exact revision, selected pages, sources, audio mode and measurement conditions alongside results.

## Initial public release checks

Source revision `5d4d4f0` built successfully in GitHub Actions. GHCR anonymous manifest retrieval and Docker pull passed for `sha256:26fa67c06a1bd5450178564d35f9e1eb0dbd84ae689947e0fae9bafa5235db84` (linux/amd64). The pulled image collected real NOAA and configured traffic RSS data and rendered frames successfully.

The graph and RSS screenshots were inspected; missing forecast bounds remain N/A. A direct network-disabled location-change exercise confirmed that changed coordinates discard the previous location's cached weather, while numerically identical coordinates retain it. An initial missing optional provider displays Waiting for data instead of an epoch timestamp.

Separate five-minute pre-release runs compared the twelve-page public candidate with a private sixteen-page extension. They exercised the optional graph and RSS pages but also changed private content and received different feed results; those runs do not isolate the public additions' individual overhead. They are not substituted for a controlled optional-page benchmark here.

## Controlled optional-page comparison

The configurable-map implementation at `4734a18` was measured on October 22, 2026 using the same AMD64 image, frozen Washington weather/radar inputs, fixed regional Houston traffic readings, silent audio, 640×480 at 30 fps, and software H.264 encoding. Three isolated containers ran concurrently on the same host, each limited to one CPU core and 512 MiB RAM. Each had a 35-second warm-up before the roughly five-minute measurement. Viewers ran in separate containers and were excluded from these memory totals.

Disabled cases omitted the unused traffic snapshot fields and map files. The graph reuses the same forecast data. No collector refresh ran during the window, so these numbers describe cached playback, not periodic collection peaks. Working set is cgroup memory minus inactive file cache, sampled once per second.

| Rotation | Mean RAM | Peak RAM | CPU, percent of one core |
|---|---:|---:|---:|
|Base twelve pages|72.10 MiB|76.25 MiB|17.60%|
|Base plus temperature graph|74.87 MiB|78.27 MiB|20.92%|
|Graph plus RSS, Flow and Bottlenecks|82.41 MiB|88.77 MiB|18.87%|

The full rotation used about **10.30 MiB more average memory than the base (14.3%)**. Traffic added about **7.54 MiB** beyond the graph-only case. CPU varies with page content and encoder input; the lower CPU of the full rotation than graph-only is not evidence that traffic improves efficiency. All three streams decoded for 370 seconds without warnings.

Separately, one real traffic refresh peaked at **41.02 MiB child-process RSS**, then exited. This measures the helper process, not incremental container working set, and must not be added to the table as an exact combined peak. Collection remains a short-lived five-minute job, independent of continuous rendering.

Validation also checked disabled map collection, missing configuration, invalid changed configuration, source-age suppression and both full traffic frames. The configured parser produced identical rows and 42 directional aggregates to the prior Houston implementation on the same feed. This run does not establish overnight stability, TV-app compatibility or idle-memory behavior.
