#!/usr/bin/env bash
set -euo pipefail

# GroundAtlas Desktop v2.3.1 — macOS App Bundle & .dmg Packager
# Works on Apple Silicon (arm64) & Intel (x86_64), both locally on a Mac and in GitHub Actions!
#
# Environment variables (optional):
#   BUILD_DIR           Path to CMake build directory (default: build-macos-rel)
#   QT_DIR              Qt 6 root dir containing bin/macdeployqt (auto-detected if on PATH)
#   MAPLIBRE_PREFIX     MapLibre Native Qt install prefix containing lib/libQMapLibreCore*.dylib
#   OUT_DIR             Output directory for .dmg + SHA256 (default: dist-macos)
#   APP_VERSION         Version string (default: 2.3.1)
#   APPLE_SIGN_IDENTITY Optional Apple Developer ID identity; defaults to "-" (ad-hoc, required on Apple Silicon)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build-macos-rel}"
OUT_DIR="${OUT_DIR:-${REPO_ROOT}/dist-macos}"
APP_VERSION="${APP_VERSION:-2.3.1}"
ARCH_NAME="${ARCH_NAME:-$(uname -m)}"
APP_NAME="GroundAtlas Desktop.app"
APP_BUNDLE="${BUILD_DIR}/${APP_NAME}"
SIGN_IDENTITY="${APPLE_SIGN_IDENTITY:--}"

if [[ ! -d "${APP_BUNDLE}" ]]; then
  echo "ERROR: App bundle not found at ${APP_BUNDLE}" >&2
  echo "Build first with CMake Release in ${BUILD_DIR}" >&2
  exit 1
fi

echo "==> [1/6] Generating Retina macOS icon (GroundAtlas.icns) from assets/groundatlas.svg"
ICONSET_DIR="$(mktemp -d)/GroundAtlas.iconset"
mkdir -p "${ICONSET_DIR}"
SVG_SRC="${REPO_ROOT}/assets/groundatlas.svg"
ICNS_DEST="${APP_BUNDLE}/Contents/Resources/GroundAtlas.icns"
mkdir -p "${APP_BUNDLE}/Contents/Resources"

render_png() {
  local size="$1"
  local out="$2"
  if command -v rsvg-convert >/dev/null 2>&1; then
    rsvg-convert -w "${size}" -h "${size}" "${SVG_SRC}" -o "${out}"
  else
    # Fallback via macOS built-in qlmanage + sips (zero external deps!)
    local tmpdir
    tmpdir="$(mktemp -d)"
    qlmanage -t -s 1024 -o "${tmpdir}" "${SVG_SRC}" >/dev/null 2>&1
    sips -z "${size}" "${size}" "${tmpdir}/groundatlas.svg.png" --out "${out}" >/dev/null
    rm -rf "${tmpdir}"
  fi
}

for sz in 16 32 128 256 512; do
  render_png "${sz}" "${ICONSET_DIR}/icon_${sz}x${sz}.png"
  render_png "$((sz * 2))" "${ICONSET_DIR}/icon_${sz}x${sz}@2x.png"
done
iconutil -c icns "${ICONSET_DIR}" -o "${ICNS_DEST}"
rm -rf "$(dirname "${ICONSET_DIR}")"

echo "==> [2/6] Copying bundled data/ and LICENSES.txt into Contents/Resources/"
rm -rf "${APP_BUNDLE}/Contents/Resources/data"
cp -R "${REPO_ROOT}/data" "${APP_BUNDLE}/Contents/Resources/data"
if [[ -f "${REPO_ROOT}/LICENSES.txt" ]]; then
  cp "${REPO_ROOT}/LICENSES.txt" "${APP_BUNDLE}/Contents/Resources/LICENSES.txt"
fi

echo "==> [3/6] Deploying Qt 6 frameworks & QMapLibreCore dylib via macdeployqt"
MACDEPLOYQT=""
if [[ -n "${QT_DIR:-}" && -x "${QT_DIR}/bin/macdeployqt" ]]; then
  MACDEPLOYQT="${QT_DIR}/bin/macdeployqt"
elif command -v macdeployqt >/dev/null 2>&1; then
  MACDEPLOYQT="$(command -v macdeployqt)"
else
  echo "ERROR: macdeployqt not found. Set QT_DIR or add Qt6 bin to PATH." >&2
  exit 1
fi

DEPLOY_ARGS=("${APP_BUNDLE}" "-always-overwrite")
if [[ -n "${MAPLIBRE_PREFIX:-}" && -d "${MAPLIBRE_PREFIX}/lib" ]]; then
  DEPLOY_ARGS+=("-libpath=${MAPLIBRE_PREFIX}/lib")
fi
"${MACDEPLOYQT}" "${DEPLOY_ARGS[@]}"

# Guarantee libQMapLibreCore*.dylib is copied and @rpath-linked inside Contents/Frameworks
if [[ -n "${MAPLIBRE_PREFIX:-}" && -d "${MAPLIBRE_PREFIX}/lib" ]]; then
  mkdir -p "${APP_BUNDLE}/Contents/Frameworks"
  for dylib in "${MAPLIBRE_PREFIX}"/lib/libQMapLibre*.dylib; do
    [[ -e "${dylib}" ]] || continue
    base="$(basename "${dylib}")"
    if [[ ! -e "${APP_BUNDLE}/Contents/Frameworks/${base}" ]]; then
      cp -a "${dylib}" "${APP_BUNDLE}/Contents/Frameworks/"
    fi
  done
fi

echo "==> [4/6] Stripping local debug symbols from main Mach-O binary"
MAIN_BIN="${APP_BUNDLE}/Contents/MacOS/GroundAtlas Desktop"
strip -x "${MAIN_BIN}" || true
echo "Binary architecture & linked libraries:"
file "${MAIN_BIN}"
otool -L "${MAIN_BIN}"

echo "==> [5/6] Signing bundle (identity: ${SIGN_IDENTITY})"
codesign --force --deep --sign "${SIGN_IDENTITY}" "${APP_BUNDLE}"
codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}"

echo "==> [6/6] Packaging compressed UDZO .dmg installer"
mkdir -p "${OUT_DIR}"
STAGE_DIR="$(mktemp -d)"
cp -R "${APP_BUNDLE}" "${STAGE_DIR}/"
ln -s /Applications "${STAGE_DIR}/Applications"
cp "${SCRIPT_DIR}/HUONG-DAN-MO-LAN-DAU.txt" "${STAGE_DIR}/HUONG-DAN-MO-LAN-DAU.txt"

DMG_NAME="GroundAtlas-Desktop-${APP_VERSION}-macOS-${ARCH_NAME}.dmg"
DMG_PATH="${OUT_DIR}/${DMG_NAME}"
rm -f "${DMG_PATH}"

hdiutil create \
  -volname "GroundAtlas Desktop ${APP_VERSION}" \
  -srcfolder "${STAGE_DIR}" \
  -ov \
  -format UDZO \
  -imagekey zlib-level=9 \
  "${DMG_PATH}"
rm -rf "${STAGE_DIR}"

(
  cd "${OUT_DIR}"
  shasum -a 256 "${DMG_NAME}" > "SHA256SUMS-macos-${ARCH_NAME}.txt"
)

echo "======================================================================"
echo "SUCCESS! macOS DMG ready:"
ls -lh "${DMG_PATH}"
cat "${OUT_DIR}/SHA256SUMS-macos-${ARCH_NAME}.txt"
echo "======================================================================"
