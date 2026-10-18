# Cross-build environment for kindlehf (Kindle FW >= 5.16.3, hard-float).
FROM --platform=linux/amd64 debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl zstd xz-utils make git autoconf automake libtool pkg-config file python3 \
    && rm -rf /var/lib/apt/lists/*
ARG KOXTC=2026.08
RUN mkdir -p /opt && curl -fsSL https://github.com/koreader/koxtoolchain/releases/download/${KOXTC}/kindlehf.tar.zst \
      | tar --zstd -x -C /opt
ENV PATH=/opt/x-tools/arm-kindlehf-linux-gnueabihf/bin:$PATH \
    CROSS_TC=arm-kindlehf-linux-gnueabihf \
    CROSS_PREFIX=arm-kindlehf-linux-gnueabihf-
WORKDIR /src
