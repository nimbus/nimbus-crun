# nimbus-crun

Patched [crun](https://github.com/containers/crun) with libkrun TSI port
mapping support for [nimbus](https://github.com/nimbus/nimbus).

## What's patched

A single patch (`0001-krun-add-tsi-port-mapping-via-oci-annotation.patch`)
adds the Nimbus `krun.port_map` annotation path to crun's krun handler. It
validates legacy `HOST_PORT:GUEST_PORT` entries, validates address-bearing
`ADDR:HOST_PORT:GUEST_PORT` and `[IPv6]:HOST_PORT:GUEST_PORT` entries, and
calls the matching libkrun TSI (Transparent Socket Impersonation) port-map
entrypoint.

Address-bearing entries fail closed unless the loaded libkrun exports
`krun_set_port_map_with_bind_address`. That keeps localhost-intended Nimbus
service bindings from silently widening to all host interfaces when the
patched libkrun lane is not present.

**Pinned versions:**
- crun: 1.29.1
- nimbus-libkrun: v1.19.4-nimbus.2
- libkrunfw bundled by nimbus-libkrun: 5.5.0
- Embedded artifact version: 1.29.1-nimbus.2
- Built with `--with-libkrun` against the Nimbus-private libkrun archive

## Published artifacts

| Artifact | Location |
|----------|----------|
| `nimbus-crun-linux-amd64` | GitHub Releases (`v*` tags) |
| `nimbus-crun-linux-arm64` | GitHub Releases (`v*` tags) |
| Build provenance | GitHub Attestations (via `actions/attest`) |

Installs to `/usr/libexec/nimbus/crun` — does not conflict with or replace
the system crun.

The binary is paired with the `nimbus-libkrun` package. Its RUNPATH is
`$ORIGIN/lib`, which resolves `/usr/libexec/nimbus/lib` after installation.
The build fails if the paired libkrun root does not export
`krun_set_port_map_with_bind_address`. `libkrun` is loaded by crun's krun
handler with `dlopen`, so it is not expected to appear in `ldd` output.

## Building locally

Requires a Linux host, a crun source checkout, and an extracted
`nimbus-libkrun` release archive:

```bash
# Clone crun at the pinned version
git clone --branch 1.29.1 --recurse-submodules \
  https://github.com/containers/crun.git /tmp/crun-src

# Download and extract the paired libkrun archive
curl -fsSL -o /tmp/nimbus-libkrun-linux-amd64.tar.gz \
  https://github.com/nimbus/nimbus-libkrun/releases/download/v1.19.4-nimbus.2/nimbus-libkrun-linux-amd64.tar.gz
mkdir -p /tmp/nimbus-libkrun
tar -xzf /tmp/nimbus-libkrun-linux-amd64.tar.gz -C /tmp/nimbus-libkrun

# Verify patch applies
bash scripts/verify-patch.sh /tmp/crun-src

# Build against nimbus-libkrun
bash scripts/build.sh \
  --source /tmp/crun-src \
  --libkrun-root /tmp/nimbus-libkrun \
  --release-version 1.29.1-nimbus.2 \
  --output /tmp/nimbus-crun

# Verify
LD_LIBRARY_PATH=/tmp/nimbus-libkrun/lib /tmp/nimbus-crun --version
```

Or use the Fedora container helper (works on macOS via Docker Desktop):

```bash
bash scripts/verify-fedora-userspace.sh \
  --crun-source /tmp/crun-src \
  --libkrun-root /tmp/nimbus-libkrun
```

## CI

The GitHub Actions workflow (`.github/workflows/build.yml`) runs:

1. **verify** — patch syntax, parser malformed-input coverage, help
   entrypoints, patch applies to crun 1.29.1
2. **build** (matrix: amd64 + arm64) — builds inside a checked-in Fedora
   builder image definition and reuses warm BuildKit `gha` cache layers so the
   dependency install step is not re-run on every workflow. Each build downloads
   the paired `nimbus-libkrun` release archive, verifies its checksum, links
   against that private root, and checks the resulting RUNPATH. The binary
   embeds the Nimbus release version, exact upstream commit, and patch-set
   digest.
3. **publish** — GitHub Release with checksums and attestation on the matching
   `v1.29.1-nimbus.*` tag

The builder image definition lives at `.github/container/Dockerfile.builder`.

## License

See [LICENSE](LICENSE).
