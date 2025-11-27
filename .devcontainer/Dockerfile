FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-22.04

# Install RocksDB and other dependencies
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
        librocksdb-dev \
        libgtest-dev \
        cmake \
        ninja-build \
        clang-format \
        clang-tidy \
        valgrind \
    && apt-get autoremove -y && apt-get clean -y \
    && rm -rf /var/lib/apt/lists/*

# Build and install Google Test
RUN cd /usr/src/gtest \
    && cmake . \
    && make \
    && cp lib/*.a /usr/lib/
