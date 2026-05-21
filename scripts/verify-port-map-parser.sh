#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cc="${CC:-cc}"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/nimbus-crun-port-map-parser.XXXXXX")"

cleanup() {
  rm -rf "${tmp_dir}"
}
trap cleanup EXIT

"${cc}" \
  -std=c11 \
  -Wall \
  -Wextra \
  -Werror \
  -o "${tmp_dir}/port-map-parser-test" \
  "${repo_root}/tests/port_map_parser_test.c"

"${tmp_dir}/port-map-parser-test"
echo "verified: krun.port_map parser malformed-input cases"
