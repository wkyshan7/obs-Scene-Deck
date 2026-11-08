#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
platform="$(uname -s)"
build_dir="${SCENE_DECK_BUILD_DIR:-$project_root/build/unix}"
configuration="${SCENE_DECK_CONFIG:-RelWithDebInfo}"
cmake_args=(-S "$project_root" -B "$build_dir" -G Ninja "-DCMAKE_BUILD_TYPE=$configuration")
if [[ "$platform" == Darwin && -f "$project_root/.deps/macos-sdk.cmake" ]]; then
  cmake_args+=(-C "$project_root/.deps/macos-sdk.cmake" "-DCMAKE_OSX_ARCHITECTURES=$(uname -m)" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0)
fi
cmake "${cmake_args[@]}" "$@"
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
mkdir -p "$project_root/dist"
(cd "$build_dir" && cpack --config CPackConfig.cmake -B "$project_root/dist")
