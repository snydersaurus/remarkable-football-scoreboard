# A host Qt for probe.sh. Not the reMarkable SDK: that SDK's own host Qt needs
# glibc 2.38 while its image is Ubuntu 22.04 (2.35), so it cannot even link
# there -- and it ships no plugins, which means no TLS and therefore no HTTPS.
# Ubuntu's packaged Qt has both, so the probe can talk to ESPN for real.
FROM --platform=linux/amd64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build ca-certificates \
        qt6-base-dev libqt6network6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
