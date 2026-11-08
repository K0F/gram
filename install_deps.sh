#!/usr/bin/env sh
# install_deps.sh — Detect Termux / Linux distribution and install any missing
# build or runtime dependencies for gram, its submodules, and postgen.
set -eu

export PATH="$PATH:/usr/bin/vendor_perl:/usr/bin/core_perl:$HOME/bin:$HOME/.local/bin"

is_termux=0
if [ "$(uname -o 2>/dev/null || true)" = "Android" ]; then
    is_termux=1
fi
case "${PREFIX:-}" in
    /data/data/com.termux/*) is_termux=1 ;;
esac

WITH_X11=0
for arg in "$@"; do
    case "$arg" in
        --with-x11) WITH_X11=1 ;;
    esac
done

have() {
    command -v "$1" >/dev/null 2>&1
}

have_raylib() {
    if have pkg-config && pkg-config --exists raylib 2>/dev/null; then
        return 0
    fi
    if [ -f /usr/include/raylib.h ] || [ -f "${PREFIX:-/usr/local}/include/raylib.h" ]; then
        return 0
    fi
    return 1
}

echo "[install_deps] Environment: $( [ "$is_termux" = "1" ] && echo "Termux (Android)" || uname -s )"

missing_cmds=""
for c in cc make pkg-config git gh go ffmpeg ffprobe sox mpv python3 yt-dlp convert; do
    if ! have "$c"; then
        missing_cmds="$missing_cmds $c"
    fi
done

need_raylib=0
if [ "$is_termux" = "0" ] || [ "$WITH_X11" = "1" ]; then
    if ! have_raylib; then
        need_raylib=1
    fi
fi

if [ -z "$missing_cmds" ] && [ "$need_raylib" = "0" ]; then
    echo "[install_deps] All required build and runtime dependencies are already installed."
    exit 0
fi

echo "[install_deps] Missing commands:${missing_cmds:- none} (need_raylib=$need_raylib)"

run_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif [ -t 0 ]; then
        sudo "$@"
    else
        sudo -n "$@"
    fi
}

if [ "$is_termux" = "1" ]; then
    pkgs=""
    ! have cc         && pkgs="$pkgs clang"
    ! have make       && pkgs="$pkgs make"
    ! have pkg-config && pkgs="$pkgs pkg-config"
    ! have git        && pkgs="$pkgs git"
    ! have gh         && pkgs="$pkgs gh"
    ! have go         && pkgs="$pkgs golang"
    ! have ffmpeg     && pkgs="$pkgs ffmpeg"
    ! have sox        && pkgs="$pkgs sox"
    ! have mpv        && pkgs="$pkgs mpv"
    ! have python3    && pkgs="$pkgs python"
    ! have yt-dlp     && pkgs="$pkgs yt-dlp"
    ! have convert    && pkgs="$pkgs imagemagick"
    ! have exiftool   && pkgs="$pkgs exiftool"
    if [ "$need_raylib" = "1" ]; then
        pkg install -y x11-repo
        pkgs="$pkgs raylib"
    fi
    if [ -n "$pkgs" ]; then
        echo "[install_deps] Installing Termux packages:$pkgs"
        # shellcheck disable=SC2086
        pkg install -y $pkgs
    fi
elif have pacman; then
    pkgs=""
    ! have cc         && pkgs="$pkgs gcc"
    ! have make       && pkgs="$pkgs make"
    ! have pkg-config && pkgs="$pkgs pkgconf"
    ! have git        && pkgs="$pkgs git"
    ! have gh         && pkgs="$pkgs github-cli"
    ! have go         && pkgs="$pkgs go"
    ! have ffmpeg     && pkgs="$pkgs ffmpeg"
    ! have sox        && pkgs="$pkgs sox"
    ! have mpv        && pkgs="$pkgs mpv"
    ! have python3    && pkgs="$pkgs python"
    ! have yt-dlp     && pkgs="$pkgs yt-dlp"
    ! have convert    && pkgs="$pkgs imagemagick"
    ! have exiftool   && pkgs="$pkgs perl-image-exiftool"
    [ "$need_raylib" = "1" ] && pkgs="$pkgs raylib"
    if [ -n "$pkgs" ]; then
        echo "[install_deps] Installing Arch/Manjaro packages:$pkgs"
        # shellcheck disable=SC2086
        run_root pacman -S --needed --noconfirm $pkgs
    fi
elif have apt-get; then
    pkgs=""
    ! have cc         && pkgs="$pkgs build-essential"
    ! have make       && pkgs="$pkgs make"
    ! have pkg-config && pkgs="$pkgs pkg-config"
    ! have git        && pkgs="$pkgs git"
    ! have gh         && pkgs="$pkgs gh"
    ! have go         && pkgs="$pkgs golang"
    ! have ffmpeg     && pkgs="$pkgs ffmpeg"
    ! have sox        && pkgs="$pkgs sox"
    ! have mpv        && pkgs="$pkgs mpv"
    ! have python3    && pkgs="$pkgs python3"
    ! have yt-dlp     && pkgs="$pkgs yt-dlp"
    ! have convert    && pkgs="$pkgs imagemagick"
    ! have exiftool   && pkgs="$pkgs libimage-exiftool-perl"
    [ "$need_raylib" = "1" ] && pkgs="$pkgs libraylib-dev"
    if [ -n "$pkgs" ]; then
        echo "[install_deps] Installing Debian/Ubuntu packages:$pkgs"
        run_root apt-get update
        # shellcheck disable=SC2086
        run_root apt-get install -y $pkgs
    fi
elif have dnf; then
    pkgs=""
    ! have cc         && pkgs="$pkgs gcc"
    ! have make       && pkgs="$pkgs make"
    ! have pkg-config && pkgs="$pkgs pkgconf-pkg-config"
    ! have git        && pkgs="$pkgs git"
    ! have gh         && pkgs="$pkgs gh"
    ! have go         && pkgs="$pkgs golang"
    ! have ffmpeg     && pkgs="$pkgs ffmpeg"
    ! have sox        && pkgs="$pkgs sox"
    ! have mpv        && pkgs="$pkgs mpv"
    ! have python3    && pkgs="$pkgs python3"
    ! have yt-dlp     && pkgs="$pkgs yt-dlp"
    ! have convert    && pkgs="$pkgs ImageMagick"
    ! have exiftool   && pkgs="$pkgs perl-Image-ExifTool"
    [ "$need_raylib" = "1" ] && pkgs="$pkgs raylib-devel"
    if [ -n "$pkgs" ]; then
        echo "[install_deps] Installing Fedora packages:$pkgs"
        # shellcheck disable=SC2086
        run_root dnf install -y $pkgs
    fi
else
    echo "[install_deps] Unsupported package manager. Please install missing tools manually:$missing_cmds" >&2
    exit 1
fi

echo "[install_deps] Done."
