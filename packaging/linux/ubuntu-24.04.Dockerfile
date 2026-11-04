FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build \
    qt6-base-dev qt6-base-dev-tools \
    libgl-dev libglu1-mesa-dev \
    libx11-dev libxext-dev libxmu-dev libxi-dev \
    libfreetype-dev libfontconfig1-dev \
    dpkg-dev fakeroot gzip file ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["bash", "/workspace/packaging/linux/build-ubuntu-24.04.sh"]
