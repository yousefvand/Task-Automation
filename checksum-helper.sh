#!/usr/bin/env bash
set -euo pipefail

VERSION="0.1.0"
OWNER="yousefvand"
REPO="Task-Automation"
PKGNAME="task-automation"

URL="https://github.com/${OWNER}/${REPO}/archive/refs/tags/v${VERSION}.tar.gz"
OUT="/tmp/${PKGNAME}-${VERSION}.tar.gz"

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "error: required command not found: $1" >&2
        exit 1
    }
}

need_cmd curl
need_cmd sha256sum

echo "==> Downloading release tarball to /tmp"
echo "Version: ${VERSION}"
echo "URL: ${URL}"
echo "Output: ${OUT}"

curl --fail --location --show-error --progress-bar "$URL" --output "$OUT"

SHA256="$(sha256sum "$OUT" | awk '{print $1}')"

echo
echo "==> SHA-256"
echo "${SHA256}  ${OUT}"
echo
echo "PKGBUILD value:"
echo "sha256sums=('${SHA256}')"
echo
echo "aur.sh usage:"
echo "SHA256SUM=${SHA256} ./aur.sh"
