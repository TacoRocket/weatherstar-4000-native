FROM debian:trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
    libcurl4-openssl-dev libxml2-dev libcpp-httplib-dev libssl-dev nlohmann-json3-dev \
    && apt-get clean
WORKDIR /build
COPY CMakeLists.txt ./
COPY src/ ./src/
RUN cmake -S . -B out -DCMAKE_BUILD_TYPE=Release && cmake --build out -j2

FROM debian:trixie-slim
RUN sed -i 's/Components: main$/Components: main non-free/' /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates tzdata ffmpeg libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-ttf-2.0-0 \
    libcurl4t64 libxml2 libcpp-httplib0.18 python3-ephem python3-pil intel-media-va-driver-non-free \
    && apt-get clean \
    && groupadd --gid 1001 weatherstar \
    && useradd --uid 1001 --gid 1001 --no-create-home weatherstar \
    && mkdir -p /app /data /stream/hls \
    && chown -R 1001:1001 /data /stream
COPY --from=build /build/out/weather-render /build/out/weather-collect /build/out/weather-serve /usr/local/bin/
COPY assets/ /app/weatherstar_assets/
COPY src/collector_helper.py /app/collector_helper.py
COPY src/traffic_map.py /app/traffic_map.py
COPY config/traffic/ /app/config/traffic/
COPY --chmod=755 scripts/start.sh /app/start.sh
COPY LICENSE /app/LICENSE
ENV TZ=America/New_York
USER 1001:1001
EXPOSE 8080
ENTRYPOINT ["/app/start.sh"]
