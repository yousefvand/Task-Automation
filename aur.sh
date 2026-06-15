#!/usr/bin/env bash
set -euo pipefail

PKGNAME="task-automation"
VERSION="0.1.0"
PKGREL="1"
OWNER="yousefvand"
REPO="Task-Automation"
GITHUB_URL="https://github.com/${OWNER}/${REPO}"
TAG="${TAG:-v${VERSION}}"
SOURCE_URL="${GITHUB_URL}/archive/refs/tags/${TAG}.tar.gz"
AUR_REMOTE="ssh://aur@aur.archlinux.org/${PKGNAME}.git"
WORKDIR="${PWD}/aur-work/${PKGNAME}"
AUR_SSH_KEY="${AUR_SSH_KEY:-${HOME}/.ssh/aur}"
GIT_SSH_COMMAND_AUR="ssh -i ${AUR_SSH_KEY} -o IdentitiesOnly=yes -o PreferredAuthentications=publickey"

# Put the checksum here after running ./checksum-helper.sh, or pass it as:
# SHA256SUM=<checksum> ./task-automation-aur-submit.sh
SHA256SUM="${SHA256SUM:-3c50b745552675fafeac69041a880b136e8ff1016d0fcfb70646e502fd99641a}"

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "error: required command not found: $1" >&2
        exit 1
    }
}

need_cmd git
need_cmd makepkg
need_cmd ssh

if [[ "${EUID}" -eq 0 ]]; then
    echo "error: do not run this script with sudo/root." >&2
    echo "Run it as your normal user so it can access your ~/.ssh/aur key." >&2
    exit 1
fi

if [[ ! -f "${AUR_SSH_KEY}" ]]; then
    echo "error: AUR SSH key not found: ${AUR_SSH_KEY}" >&2
    echo "Set AUR_SSH_KEY=/path/to/key if your key is somewhere else." >&2
    exit 1
fi

if [[ -z "${SHA256SUM}" ]]; then
    cat >&2 <<MSG
error: SHA256SUM is empty.

Run:
  ./checksum-helper.sh

Then run:
  SHA256SUM=<sha256-from-helper> ./task-automation-aur-submit.sh

Or edit this script and set SHA256SUM near the top.
MSG
    exit 1
fi

echo "==> Package: ${PKGNAME} ${VERSION}-${PKGREL}"
echo "==> Source:  ${SOURCE_URL}"
echo "==> AUR:     ${AUR_REMOTE}"
echo "==> SSH key: ${AUR_SSH_KEY}"

mkdir -p "$(dirname "${WORKDIR}")"

if [[ -d "${WORKDIR}/.git" ]]; then
    echo "==> Updating existing AUR working tree: ${WORKDIR}"
    GIT_SSH_COMMAND="${GIT_SSH_COMMAND_AUR}" git -C "${WORKDIR}" pull --ff-only || true
else
    echo "==> Cloning AUR repository: ${AUR_REMOTE}"
    GIT_SSH_COMMAND="${GIT_SSH_COMMAND_AUR}" git clone "${AUR_REMOTE}" "${WORKDIR}"
fi

cd "${WORKDIR}"

cat > PKGBUILD <<PKGBUILD
# Maintainer: Remisa Phillips <remisa.yousefvand@gmail.com>

pkgname=${PKGNAME}
pkgver=${VERSION}
pkgrel=${PKGREL}
pkgdesc='KDE Plasma Wayland task recorder and player for keyboard, mouse, wheel, and timing automation'
arch=('x86_64')
url='${GITHUB_URL}'
license=('MIT')
depends=('qt6-base' 'kpackage' 'kconfig')
makedepends=('cmake')
source=("${REPO}-\${pkgver}.tar.gz::${SOURCE_URL}")
sha256sums=('${SHA256SUM}')

build() {
    cmake -S "${REPO}-\${pkgver}" -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -Wno-dev
    cmake --build build
}

package() {
    DESTDIR="\${pkgdir}" cmake --install build

    install -Dm644 "${REPO}-\${pkgver}/LICENSE" \
        "\${pkgdir}/usr/share/licenses/\${pkgname}/LICENSE"

    install -Dm644 "${REPO}-\${pkgver}/packaging/linux/taskautomation.desktop" \
        "\${pkgdir}/usr/share/applications/taskautomation.desktop"

    install -Dm644 "${REPO}-\${pkgver}/resources/icons/taskautomation.png" \
        "\${pkgdir}/usr/share/pixmaps/taskautomation.png"

    install -Dm644 "${REPO}-\${pkgver}/packaging/udev/70-taskautomation.rules" \
        "\${pkgdir}/usr/lib/udev/rules.d/70-taskautomation.rules"
}
PKGBUILD

makepkg --printsrcinfo > .SRCINFO

echo
echo "==> Generated AUR package files in: ${WORKDIR}"
echo "==> Files to submit:"
ls -la PKGBUILD .SRCINFO

echo
echo "==> PKGBUILD source:"
grep -E '^(pkgname|pkgver|source|sha256sums)=' PKGBUILD

echo
read -r -p "Commit and push to AUR now? [y/N] " answer
case "${answer}" in
    y|Y|yes|YES)
        git add PKGBUILD .SRCINFO
        git commit -m "Update to ${VERSION}" || true
        GIT_SSH_COMMAND="${GIT_SSH_COMMAND_AUR}" git push
        echo "==> Submitted to AUR as ${PKGNAME}."
        ;;
    *)
        echo "==> Not pushed. Review files in: ${WORKDIR}"
        ;;
esac
