#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
build_dir="${repo_root}/Build/lvgl-ui-host-tests"
cc="${CC:-gcc}"

mkdir -p "${build_dir}"

common_cflags=(
  -std=c11
  -Wall
  -Wextra
  -Wno-unused-parameter
  -DLV_CONF_PATH="${repo_root}/Tests/Host/lv_conf.h"
  -I"${repo_root}/Include"
  -I"${repo_root}/Library/LvglLib"
  -I"${repo_root}/Library/LvglLib/lvgl"
)

lvgl_objects=()
source_index=0
while IFS= read -r source; do
  object="${build_dir}/lvgl_${source_index}.o"
  "${cc}" "${common_cflags[@]}" -c "${source}" -o "${object}"
  lvgl_objects+=("${object}")
  source_index=$((source_index + 1))
done < <(find "${repo_root}/Library/LvglLib/lvgl/src" -type f -name '*.c' | sort)

"${cc}" "${common_cflags[@]}" -Werror \
  -c "${repo_root}/Library/LvglUi/LvglUiControls.c" \
  -o "${build_dir}/LvglUiControls.o"
"${cc}" "${common_cflags[@]}" -Werror \
  -c "${repo_root}/Tests/LvglUiControlsHostTest.c" \
  -o "${build_dir}/LvglUiControlsHostTest.o"

"${cc}" "${lvgl_objects[@]}" \
  "${build_dir}/LvglUiControls.o" \
  "${build_dir}/LvglUiControlsHostTest.o" \
  -lm \
  -o "${build_dir}/lvgl_ui_controls_host_test"

"${build_dir}/lvgl_ui_controls_host_test"
