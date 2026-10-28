# Build image for the ProsperoSunshine PS5 app (app/build.sh). The app is built with the
# ps5-homebrew-ui kit's own pipeline, which wants Clang 18, Ninja and the tools its scripts call.
FROM debian:trixie
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    bash ca-certificates clang-18 lld-18 llvm-18 libclang-rt-18-dev ninja-build make git wget curl \
    unzip python3 rsync xz-utils \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /repo
