FROM debian:trixie
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    bash ca-certificates clang-18 lld-18 llvm-18-dev cmake ninja-build git curl unzip python3 ffmpeg \
    && rm -rf /var/lib/apt/lists/*
ARG SDK_VERSION=v0.45
ARG SDK_SHA256=5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c
RUN curl -fL "https://github.com/ps5-payload-dev/sdk/releases/download/${SDK_VERSION}/ps5-payload-sdk.zip" -o /tmp/sdk.zip \
    && echo "${SDK_SHA256}  /tmp/sdk.zip" | sha256sum -c - \
    && unzip -q /tmp/sdk.zip -d /opt && rm /tmp/sdk.zip
ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
RUN apt-get update -qq && apt-get install -y --no-install-recommends libclang-rt-18-dev \
    && rm -rf /var/lib/apt/lists/*
# OpenSSL's Configure generates a Makefile.
RUN apt-get update -qq && apt-get install -y --no-install-recommends make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /repo
