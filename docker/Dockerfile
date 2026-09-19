# The reMarkable SDK is a Yocto toolchain that only runs on x86_64 Linux.
# On an Apple Silicon Mac this image runs under emulation via --platform.
FROM --platform=linux/amd64 ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        file \
        python3 \
        rsync \
        bzip2 \
        xz-utils \
        ca-certificates \
        libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# Drop the SDK installer in next to this Dockerfile before building.
ARG SDK_INSTALLER=rmpp-sdk.sh
COPY ${SDK_INSTALLER} /tmp/sdk.sh

RUN chmod +x /tmp/sdk.sh \
    && /tmp/sdk.sh -y -d /opt/rmpp-sdk \
    && rm -f /tmp/sdk.sh

WORKDIR /src
