#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
usage: build-nimbus-crun.sh --source <path> --libkrun-root <path> [--output <path>] [--install-path <path>] [--sudo-install] [--build-dir <path>] [--runpath <value>]

Build the checked-in patched nimbus crun binary from a pinned upstream crun
source checkout. This helper is Linux-only and preserves the original source
checkout by building from a copied worktree.

examples:
  bash scripts/build-nimbus-crun.sh \
    --source ~/src/github.com/containers/crun \
    --libkrun-root /tmp/nimbus-libkrun \
    --output /tmp/nimbus-crun-stage/crun

  bash scripts/build-nimbus-crun.sh \
    --source ~/src/github.com/containers/crun \
    --libkrun-root /tmp/nimbus-libkrun \
    --output /tmp/nimbus-crun-stage/crun \
    --install-path /usr/libexec/nimbus/crun \
    --sudo-install
EOF
}

require_command() {
  local command_name="$1"

  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "required command not found: ${command_name}" >&2
    exit 69
  fi
}

resolve_existing_dir() {
  local dir_path="$1"

  if [[ ! -d "${dir_path}" ]]; then
    echo "directory not found: ${dir_path}" >&2
    exit 66
  fi

  (
    cd "${dir_path}"
    pwd
  )
}

find_libkrun_shared_object() {
  local libkrun_root="$1"
  local candidate

  for candidate in \
    "${libkrun_root}/lib/libkrun.so.1.18.1" \
    "${libkrun_root}/lib/libkrun.so.1" \
    "${libkrun_root}/lib/libkrun.so"; do
    if [[ -e "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  return 1
}

verify_libkrun_root() {
  local libkrun_root="$1"
  local pc_file="${libkrun_root}/lib/pkgconfig/libkrun.pc"
  local shared_object
  local nm_output
  local pkg_config_libs

  if [[ ! -f "${pc_file}" ]]; then
    echo "nimbus-libkrun pkg-config metadata not found: ${pc_file}" >&2
    exit 66
  fi

  if ! shared_object="$(find_libkrun_shared_object "${libkrun_root}")"; then
    echo "nimbus-libkrun shared object not found under: ${libkrun_root}/lib" >&2
    exit 66
  fi

  nm_output="$(nm -D "${shared_object}")"
  if [[ "${nm_output}" != *" krun_set_port_map_with_bind_address"* ]]; then
    echo "nimbus-libkrun is missing krun_set_port_map_with_bind_address: ${shared_object}" >&2
    exit 69
  fi

  export PKG_CONFIG_PATH="${libkrun_root}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  if ! pkg-config --exists libkrun 2>/dev/null; then
    echo "nimbus-libkrun pkg-config metadata is not resolvable: ${pc_file}" >&2
    exit 69
  fi

  pkg_config_libs="$(pkg-config --define-prefix --libs libkrun)"
  if [[ " ${pkg_config_libs} " != *" -L${libkrun_root}/lib "* ]]; then
    echo "libkrun pkg-config libs do not resolve to the nimbus-libkrun root: ${pkg_config_libs}" >&2
    exit 69
  fi

  echo "build.libkrun.root=${libkrun_root}"
  echo "build.libkrun.include=${libkrun_root}/include"
  echo "build.libkrun.shared_object=${shared_object}"
  echo "build.libkrun.pkg_config=${pkg_config_libs}"
}

verify_binary_linkage() {
  local binary_path="$1"
  local libkrun_root="$2"
  local expected_runpath="$3"
  local dynamic_entries
  local ldd_output

  dynamic_entries="$(readelf -d "${binary_path}")"

  if [[ -n "${expected_runpath}" && "${dynamic_entries}" != *"${expected_runpath}"* ]]; then
    echo "built crun binary is missing expected RUNPATH/RPATH: ${expected_runpath}" >&2
    echo "${dynamic_entries}" >&2
    exit 70
  fi

  if [[ "${dynamic_entries}" == *"Shared library: [libkrun.so.1]"* ]]; then
    ldd_output="$(LD_LIBRARY_PATH="${libkrun_root}/lib:${LD_LIBRARY_PATH:-}" ldd "${binary_path}")"
    echo "${ldd_output}"
    if [[ "${ldd_output}" != *"libkrun.so.1 => ${libkrun_root}/lib/"* ]]; then
      echo "built crun binary does not resolve libkrun from nimbus-libkrun root" >&2
      exit 70
    fi
  else
    echo "build.libkrun.linkage=dlopen"
  fi
}

source_dir=""
libkrun_root=""
output_path="${TMPDIR:-/tmp}/nimbus-crun-stage/crun"
install_path=""
build_dir=""
sudo_install=0
runpath='$ORIGIN/lib'

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source)
      source_dir="${2:-}"
      shift 2
      ;;
    --libkrun-root)
      libkrun_root="${2:-}"
      shift 2
      ;;
    --output)
      output_path="${2:-}"
      shift 2
      ;;
    --install-path)
      install_path="${2:-}"
      shift 2
      ;;
    --build-dir)
      build_dir="${2:-}"
      shift 2
      ;;
    --runpath)
      runpath="${2:-}"
      shift 2
      ;;
    --sudo-install)
      sudo_install=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 64
      ;;
  esac
done

if [[ -z "${source_dir}" ]]; then
  usage >&2
  exit 64
fi

if [[ -z "${libkrun_root}" ]]; then
  usage >&2
  exit 64
fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "build-nimbus-crun.sh requires a Linux host" >&2
  exit 69
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
patch_dir="${repo_root}/patches"

if [[ ! -d "${source_dir}" ]]; then
  echo "upstream crun source not found: ${source_dir}" >&2
  exit 66
fi
source_dir="$(resolve_existing_dir "${source_dir}")"
libkrun_root="$(resolve_existing_dir "${libkrun_root}")"

if [[ ! -d "${patch_dir}" ]]; then
  echo "patch directory not found: ${patch_dir}" >&2
  exit 66
fi

require_command bash
require_command patch
require_command make
require_command autoreconf
require_command autoconf
require_command automake
require_command grep
require_command pkg-config
require_command cp
require_command ldd
require_command nm
require_command patchelf
require_command readelf
require_command install

if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
  echo "required C compiler not found (cc, gcc, or clang)" >&2
  exit 69
fi

verify_libkrun_root "${libkrun_root}"

# Dry-run all patches to verify they apply cleanly before building.
for patch_file in "${patch_dir}"/*.patch; do
  if [[ ! -f "${patch_file}" ]]; then
    echo "no patch files found in: ${patch_dir}" >&2
    exit 66
  fi
  patch --dry-run -d "${source_dir}" -p1 < "${patch_file}"
  echo "verified: ${patch_file} applies cleanly to ${source_dir}"
done

cleanup_build_dir=0
if [[ -z "${build_dir}" ]]; then
  build_dir="$(mktemp -d "${TMPDIR:-/tmp}/nimbus-crun-build.XXXXXX")"
  cleanup_build_dir=1
fi

mkdir -p "${build_dir}"
if find "${build_dir}" -mindepth 1 -maxdepth 1 | read -r _; then
  echo "build directory must be empty: ${build_dir}" >&2
  exit 64
fi

cleanup() {
  if [[ "${cleanup_build_dir}" -eq 1 ]]; then
    rm -rf "${build_dir}"
  fi
}
trap cleanup EXIT

cp -R "${source_dir}/." "${build_dir}"

echo "build.source=${source_dir}"
echo "build.dir=${build_dir}"
echo "build.patch_dir=${patch_dir}"

for patch_file in "${patch_dir}"/*.patch; do
  echo "build.applying=${patch_file}"
  patch -d "${build_dir}" -p1 < "${patch_file}"
done

export LD_LIBRARY_PATH="${libkrun_root}/lib:${LD_LIBRARY_PATH:-}"
export CPPFLAGS="-I${libkrun_root}/include ${CPPFLAGS:-}"
export LDFLAGS="-Wl,-rpath,${runpath} ${LDFLAGS:-}"

(
  cd "${build_dir}"
  ./autogen.sh
  ./configure --with-libkrun
  # Build the generated libocispec and git-version prerequisites explicitly.
  # This matches upstream's packaged build flow and avoids fresh-worktree
  # races around generated headers.
  make -j1 -C libocispec libocispec.la
  make -j1 git-version.h
  make -j1 crun
)

built_binary="${build_dir}/crun"
if [[ ! -x "${built_binary}" ]]; then
  echo "expected built binary not found: ${built_binary}" >&2
  exit 70
fi

if [[ -n "${runpath}" ]]; then
  patchelf --set-rpath "${runpath}" "${built_binary}"
fi
verify_binary_linkage "${built_binary}" "${libkrun_root}" "${runpath}"

mkdir -p "$(dirname "${output_path}")"
install -m 0755 "${built_binary}" "${output_path}"
echo "build.output=${output_path}"

verify_binary_linkage "${output_path}" "${libkrun_root}" "${runpath}"

version_line="$(LD_LIBRARY_PATH="${libkrun_root}/lib:${LD_LIBRARY_PATH:-}" "${output_path}" --version 2>/dev/null | head -n1 || true)"
if [[ -n "${version_line}" ]]; then
  echo "build.output.version=${version_line}"
fi

if [[ -n "${install_path}" ]]; then
  if [[ "${sudo_install}" -eq 1 ]]; then
    sudo mkdir -p "$(dirname "${install_path}")"
    sudo install -m 0755 "${output_path}" "${install_path}"
  else
    mkdir -p "$(dirname "${install_path}")"
    install -m 0755 "${output_path}" "${install_path}"
  fi
  echo "build.install=${install_path}"

  verify_binary_linkage "${install_path}" "${libkrun_root}" "${runpath}"

  install_version_line="$(LD_LIBRARY_PATH="${libkrun_root}/lib:${LD_LIBRARY_PATH:-}" "${install_path}" --version 2>/dev/null | head -n1 || true)"
  if [[ -n "${install_version_line}" ]]; then
    echo "build.install.version=${install_version_line}"
  fi
fi
