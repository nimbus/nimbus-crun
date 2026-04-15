# neovex-crun

Patched [crun](https://github.com/containers/crun) with libkrun TSI port
mapping support for [neovex](https://github.com/agentstation/neovex).

## What's patched

A single patch (`0001-krun-add-tsi-port-mapping-via-oci-annotation.patch`)
adds ~50 lines to crun's krun handler. It reads a `krun.port_map` OCI
annotation and calls `krun_set_port_map()` to configure TSI (Transparent
Socket Impersonation) port forwarding for libkrun microVMs.

**Pinned versions:**
- crun: 1.27
- Built with `--with-libkrun` (libkrun-devel from Fedora 43 repos)

## Published artifacts

| Artifact | Location |
|----------|----------|
| `neovex-crun-linux-amd64` | GitHub Releases (`v*` tags) |
| `neovex-crun-linux-arm64` | GitHub Releases (`v*` tags) |
| Build provenance | GitHub Attestations (via `actions/attest`) |

Installs to `/usr/libexec/neovex/crun` — does not conflict with or replace
the system crun.

## Building locally

Requires a Fedora host (or Docker/Podman with Fedora) and a crun source
checkout:

```bash
# Clone crun at the pinned version
git clone --branch 1.27 --recurse-submodules \
  https://github.com/containers/crun.git /tmp/crun-src

# Verify patch applies
bash scripts/verify-patch.sh /tmp/crun-src

# Build (requires libkrun-devel)
bash scripts/build.sh \
  --source /tmp/crun-src \
  --output /tmp/neovex-crun

# Verify
/tmp/neovex-crun --version  # should show +LIBKRUN
```

Or use the Fedora container helper (works on macOS via Docker Desktop):

```bash
bash scripts/verify-fedora-userspace.sh --crun-source /tmp/crun-src
```

## CI

The GitHub Actions workflow (`.github/workflows/build.yml`) runs:

1. **verify** — patch syntax, help entrypoints, patch applies to crun 1.27
2. **build** (matrix: amd64 + arm64) — builds inside a checked-in Fedora
   builder image definition and reuses warm BuildKit `gha` cache layers so the
   dependency install step is not re-run on every workflow
3. **publish** — GitHub Release with checksums and attestation on `v*` tags

The builder image definition lives at `.github/container/Dockerfile.builder`.

## License

See [LICENSE](LICENSE).
