#!/usr/bin/env bash

set -euo pipefail

# ----------------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------------
NWIPE_REPO="${NWIPE_REPO:-https://github.com/kiwi-x86/nwipe.git}"
NWIPE_REF="${NWIPE_REF:-}"          # empty = clone the repo's default branch
WITH_NVME="${WITH_NVME:-yes}"
KEEP_SRC="${KEEP_SRC:-no}"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWARN:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# ----------------------------------------------------------------------------
# Guardrails
# ----------------------------------------------------------------------------
[ "$(id -u)" -eq 0 ] || die "must run as root (installs packages + writes to /usr/local). Re-run with sudo."

# ----------------------------------------------------------------------------
# Detect the package manager and install build + runtime dependencies
# ----------------------------------------------------------------------------
install_deps() {
    if command -v dnf >/dev/null 2>&1; then
        log "Detected dnf (RHEL/Rocky/Alma/Fedora). Installing dependencies…"

        # EPEL + CRB (CodeReady Builder) hold some -devel packages
        # (e.g. libconfig-devel) on RHEL 9 clones.
        dnf install -y epel-release || warn "epel-release not installed (Fedora may not need it)."
        dnf install -y 'dnf-command(config-manager)' >/dev/null 2>&1 || true
        dnf config-manager --set-enabled crb 2>/dev/null \
            || dnf config-manager --set-enabled powertools 2>/dev/null \
            || warn "could not enable CRB/PowerTools; libconfig-devel may be unavailable."

        dnf groupinstall -y "Development Tools" \
            || dnf group install -y "Development Tools" \
            || warn "could not install the Development Tools group; assuming gcc/make are present."

        # Build deps + runtime helpers nwipe shells out to:
        #   dmidecode  -> SMBIOS / serial data
        #   smartmontools -> SMART info
        #   hdparm     -> HPA/DCO + ATA secure erase
        #   screen     -> keep long wipes alive over SSH
        local pkgs=(git ncurses-devel parted-devel libconfig-devel
                    dmidecode coreutils smartmontools hdparm screen)
        [ "$WITH_NVME" = yes ] && pkgs+=(libnvme-devel)
        dnf install -y "${pkgs[@]}"

    elif command -v apt-get >/dev/null 2>&1; then
        log "Detected apt (Debian/Ubuntu). Installing dependencies…"
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        local pkgs=(build-essential autoconf automake libtool pkg-config git
                    libncurses-dev libconfig-dev libparted-dev
                    uuid-dev libdevmapper-dev libblkid-dev
                    dmidecode coreutils smartmontools hdparm screen)
        [ "$WITH_NVME" = yes ] && pkgs+=(libnvme-dev)
        apt-get install -y -qq "${pkgs[@]}"
    else
        die "no supported package manager found (need dnf, yum, or apt-get). Install build deps manually."
    fi
}

# ----------------------------------------------------------------------------
# Build + install
# ----------------------------------------------------------------------------
build_nwipe() {
    local workdir; workdir=$(mktemp -d /tmp/nwipe-build.XXXXXX)
    if [ -n "$NWIPE_REF" ]; then
        log "Cloning $NWIPE_REPO (ref: $NWIPE_REF) into $workdir …"
        git clone --depth 1 --branch "$NWIPE_REF" "$NWIPE_REPO" "$workdir/nwipe" \
            || die "git clone failed. Check NWIPE_REPO / NWIPE_REF."
    else
        log "Cloning $NWIPE_REPO (default branch) into $workdir …"
        git clone --depth 1 "$NWIPE_REPO" "$workdir/nwipe" \
            || die "git clone failed. Check NWIPE_REPO."
    fi
    cd "$workdir/nwipe"

    log "Running autogen + configure…"
    ./autogen.sh
    local cfg_args=()
    [ "$WITH_NVME" = no ] && cfg_args+=(--without-libnvme)
    ./configure "${cfg_args[@]}"

    log "Compiling (make -j$(nproc))…"
    make -j"$(nproc)"

    log "Installing to /usr/local…"
    make install
    command -v ldconfig >/dev/null 2>&1 && ldconfig || true

    if [ "$KEEP_SRC" = yes ]; then
        log "Build tree kept at $workdir/nwipe"
    else
        cd /
        rm -rf "$workdir"
    fi
}

# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------
main() {
    log "nwipe source installer (repo: $NWIPE_REPO)"
    install_deps
    build_nwipe

    local bin; bin=$(command -v nwipe || echo /usr/local/sbin/nwipe)
    log "Done. Installed: $bin"
    "$bin" --version || true
    cat <<'EOF'

nwipe is installed. It requires root and a real terminal:

    sudo nwipe            # scan all disks, interactive GUI
    sudo nwipe --help     # options

WARNING: nwipe DESTROYS DATA. It is a disk wiper. Double-check the
target device before starting a wipe on any real machine.
EOF
}

main "$@"
