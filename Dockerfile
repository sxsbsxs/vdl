FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY backend ./backend
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel 2

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl unzip python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://deno.land/install.sh -o /tmp/deno-install.sh \
    && DENO_INSTALL=/usr/local sh /tmp/deno-install.sh \
    && rm -f /tmp/deno-install.sh

RUN python3 -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir --upgrade "yt-dlp[default]"

WORKDIR /app
COPY --from=build /src/build/vdl-server /app/vdl-server
COPY frontend /app/frontend

ENV PORT=10000
ENV YTDLP_BIN=/opt/venv/bin/yt-dlp
ENV PATH="/usr/local/bin:/opt/venv/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin"
EXPOSE 10000
CMD ["/app/vdl-server"]
