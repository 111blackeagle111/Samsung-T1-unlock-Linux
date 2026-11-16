FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential meson ninja-build pkg-config cmake file wget \
    libgtkmm-3.0-dev libgudev-1.0-dev libusb-1.0-0-dev libgtest-dev \
    librsvg2-bin xvfb \
 && rm -rf /var/lib/apt/lists/*
