# Cloudflare Containers currently require linux/amd64 images.
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl cmake g++ make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY backend ./backend

RUN mkdir -p vendor \
    && curl -fsSL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.27.0/httplib.h -o vendor/httplib.h \
    && curl -fsSL https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp -o vendor/json.hpp \
    && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release -j2

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
ENV PORT=10000
ENV HOME=/tmp
ENV XDG_CACHE_HOME=/tmp/.cache
ENV DENO_INSTALL=/usr/local

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl ffmpeg libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# yt-dlp_linux is the upstream standalone x86_64 release binary.
RUN curl -fL --retry 3 https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux \
      -o /usr/local/bin/yt-dlp \
    && chmod 0755 /usr/local/bin/yt-dlp

# Deno is used by yt-dlp for JavaScript-dependent extractors.
RUN curl -fsSL https://deno.land/install.sh -o /tmp/deno-install.sh \
    && sh /tmp/deno-install.sh \
    && rm -f /tmp/deno-install.sh

COPY --from=build /src/build/videodl-api /usr/local/bin/videodl-api

EXPOSE 10000
USER 65532:65532
ENTRYPOINT ["/usr/local/bin/videodl-api"]
