#!/bin/bash
# Cross-compile bin/emu-manager (aarch64) via Docker. Run on a dev box with Docker.
set -e
cd "$(dirname "$0")/.."
mkdir -p bin
docker run --rm --platform linux/arm64 -v "$PWD:/build" -w /build debian:bookworm-slim sh -c '
  apt-get update -qq && apt-get install -y -qq gcc libsdl2-dev pkg-config
  gcc -O2 -Wall src/emu-manager.c -o bin/emu-manager $(pkg-config --cflags --libs sdl2) -lm
'
file bin/emu-manager
