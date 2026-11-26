#!/bin/sh
set -eu

repo=https://github.com/mattmezza/lok
bin=/usr/local/bin
libexec=/usr/local/libexec
man=/usr/local/share/man/man1
pam=/etc/pam.d/lok-fingerprint

fail() {
    printf 'lok installer: %s\n' "$*" >&2
    exit 1
}

download() {
    curl -q --proto '=https' --proto-redir '=https' -fsSL --retry 3 "$@"
}

trusted_dir() {
    [ "$1" != / ] || return 0
    parent=${1%/*}
    trusted_dir "${parent:-/}"
    [ ! -L "$1" ] || fail "expected a directory at $1"
    mkdir -p -- "$1"
    metadata=$(stat -c '%u %a' -- "$1")
    [ "${metadata%% *}" = 0 ] && [ "$((0${metadata#* } & 022))" -eq 0 ] ||
        fail "$1 must be owned by root and writable only by root"
}

stage_file() {
    install -o 0 -g 0 -m "$2" -- "$stage/$3" "$1"
}

main() {
    action=${1:-install}
    tag=${2:-}
    case "$action" in
        install|upgrade) [ "$#" -le 2 ] || fail "usage: install.sh [install|upgrade [vX.Y.Z] | uninstall]" ;;
        uninstall) [ "$#" -eq 1 ] || fail "usage: install.sh uninstall" ;;
        *) fail "usage: install.sh [install|upgrade [vX.Y.Z] | uninstall]" ;;
    esac
    [ "$(id -u)" -eq 0 ] || fail "run this script with sudo"

    umask 022
    trusted_dir /usr/local
    exec 9< /usr/local
    flock -x 9
    man=$(realpath -m -- "$man")

    if [ "$action" = uninstall ]; then
        rm -f -- "$bin/lok" "$libexec/lok-pam" "$man/lok.1"
        printf 'Removed lok. The fingerprint PAM policy is kept at %s.\n' "$pam"
        return
    fi

    case "$(uname -s):$(uname -m)" in
        Linux:x86_64) archive=lok-linux-x86_64.tar.gz ;;
        Linux:aarch64|Linux:arm64) archive=lok-linux-aarch64.tar.gz ;;
        *) fail "no binary for $(uname -s) $(uname -m)" ;;
    esac
    if [ -z "$tag" ]; then
        latest=$(curl -q --proto '=https' -fsS --retry 3 -o /dev/null -w '%{redirect_url}' "$repo/releases/latest")
        case "$latest" in
            "$repo/releases/tag/"*) tag=${latest##*/} ;;
            *) fail "could not find the latest published release" ;;
        esac
    fi
    case "$tag" in *[!v0-9.]*) fail "expected a release tag such as v0.1.0" ;; esac
    printf '%s\n' "$tag" | LC_ALL=C grep -Exq 'v(0|[1-9][0-9]*)(\.(0|[1-9][0-9]*)){2}' ||
        fail "expected a release tag such as v0.1.0"

    stage=$(mktemp -d)
    bin_tmp= helper_tmp= man_tmp= pam_tmp=
    trap 'rm -rf -- "$stage"; rm -f -- "$bin_tmp" "$helper_tmp" "$man_tmp" "$pam_tmp"' EXIT
    trap 'exit 1' HUP INT TERM

    base="$repo/releases/download/$tag"
    download "$base/SHA256SUMS" -o "$stage/SHA256SUMS"
    digest=$(awk -v archive="$archive" '$2 == archive { print $1 }' "$stage/SHA256SUMS")
    [ "${#digest}" -eq 64 ] || fail "missing or ambiguous checksum for $archive"
    case "$digest" in *[!0-9a-f]*) fail "invalid checksum for $archive" ;; esac
    download "$base/$archive" -o "$stage/$archive"
    (cd "$stage" && printf '%s  %s\n' "$digest" "$archive" | sha256sum -c -)
    tar -xzf "$stage/$archive" -C "$stage" -- lok lok-pam lok.1 lok-fingerprint

    for dir in "$bin" "$libexec" "$man" /etc/pam.d; do
        trusted_dir "$dir"
    done
    for target in "$bin/lok" "$libexec/lok-pam" "$man/lok.1"; do
        [ ! -d "$target" ] || fail "$target is a directory"
    done

    bin_tmp=$(mktemp "$bin/.lok.XXXXXXXX")
    helper_tmp=$(mktemp "$libexec/.lok-pam.XXXXXXXX")
    man_tmp=$(mktemp "$man/.lok.1.XXXXXXXX")
    stage_file "$bin_tmp" 4755 lok
    stage_file "$helper_tmp" 0700 lok-pam
    stage_file "$man_tmp" 0644 lok.1

    if [ ! -e "$pam" ] && [ ! -L "$pam" ]; then
        pam_tmp=$(mktemp "/etc/pam.d/.lok-fingerprint.XXXXXXXX")
        stage_file "$pam_tmp" 0644 lok-fingerprint
        if ! ln -T -- "$pam_tmp" "$pam" && [ ! -e "$pam" ] && [ ! -L "$pam" ]; then
            fail "could not install $pam"
        fi
    fi

    mv -fT -- "$man_tmp" "$man/lok.1"
    mv -fT -- "$helper_tmp" "$libexec/lok-pam"
    mv -fT -- "$bin_tmp" "$bin/lok"
    printf 'Installed lok %s in %s/lok\n' "$tag" "$bin"
}

main "$@"
