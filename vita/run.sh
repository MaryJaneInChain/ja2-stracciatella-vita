#!/bin/sh
# Run a command inside the VitaSDK+Rust container, repository mounted at /work
exec podman run --rm --network=host -v "$(dirname "$(readlink -f "$0")")/..":/work:Z -w /work \
  -v stracciatella-vita-cargo:/opt/cargo/registry stracciatella-vita-sdk sh -c "$*"
