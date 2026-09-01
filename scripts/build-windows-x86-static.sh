#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

QT_VERSION=5.9.9
QT_ARCHIVE_NAME="qtbase-opensource-src-${QT_VERSION}.tar.xz"
QT_ARCHIVE_URL="https://download.qt.io/new_archive/qt/5.9/5.9.9/submodules/${QT_ARCHIVE_NAME}"
QT_ARCHIVE_SHA256="d5a97381b9339c0fbaf13f0c05d599a5c999dcf94145044058198987183fed65"

BUILD_ROOT=${BUILD_ROOT:-"$PROJECT_DIR/.build/windows-x86-static"}
DOWNLOAD_DIR="$BUILD_ROOT/downloads"
QT_SOURCE_DIR="$BUILD_ROOT/qtbase-src-${QT_VERSION}"
QT_BUILD_DIR="$BUILD_ROOT/qtbase-build-${QT_VERSION}"
QT_INSTALL_DIR="$BUILD_ROOT/qtbase-static-${QT_VERSION}"
APP_BUILD_DIR="$BUILD_ROOT/fxpresser-build"
OUTPUT_DIR="$PROJECT_DIR/dist/windows-x86-static"
OUTPUT_ZIP="$PROJECT_DIR/dist/FxPresser-windows-x86-static.zip"
JOBS=${JOBS:-4}

required_commands=(
    curl sha256sum tar patch perl gmake install file
    i686-w64-mingw32-gcc i686-w64-mingw32-g++
    i686-w64-mingw32-ar i686-w64-mingw32-windres
    zip
)

missing_commands=()
for command_name in "${required_commands[@]}"; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        missing_commands+=("$command_name")
    fi
done

if ((${#missing_commands[@]})); then
    printf '缺少构建命令：%s\n' "${missing_commands[*]}" >&2
    printf 'Debian/Ubuntu 可执行：sudo apt install build-essential curl xz-utils patch perl zip g++-mingw-w64-i686\n' >&2
    exit 1
fi

case "$BUILD_ROOT" in
    "$PROJECT_DIR"/.build/*|/tmp/*) ;;
    *)
        printf 'BUILD_ROOT 必须位于 %s/.build/ 或 /tmp/ 下：%s\n' "$PROJECT_DIR" "$BUILD_ROOT" >&2
        exit 1
        ;;
esac

mkdir -p "$DOWNLOAD_DIR" "$QT_SOURCE_DIR" "$QT_BUILD_DIR" "$QT_INSTALL_DIR" "$APP_BUILD_DIR" "$OUTPUT_DIR"

QT_ARCHIVE="$DOWNLOAD_DIR/$QT_ARCHIVE_NAME"
if [[ ! -f "$QT_ARCHIVE" ]] || ! printf '%s  %s\n' "$QT_ARCHIVE_SHA256" "$QT_ARCHIVE" | sha256sum --check --status; then
    printf '下载 Qt %s 源码……\n' "$QT_VERSION"
    curl --location --fail --retry 5 --retry-all-errors --continue-at - \
        --output "$QT_ARCHIVE" "$QT_ARCHIVE_URL"
fi
printf '%s  %s\n' "$QT_ARCHIVE_SHA256" "$QT_ARCHIVE" | sha256sum --check

SOURCE_READY_MARKER="$QT_SOURCE_DIR/.fxpresser-source-ready"
if [[ ! -f "$SOURCE_READY_MARKER" ]]; then
    printf '解压 Qt 源码……\n'
    tar -xf "$QT_ARCHIVE" -C "$QT_SOURCE_DIR" --strip-components=1
    touch "$SOURCE_READY_MARKER"
fi

PATCH_FILE="$PROJECT_DIR/patches/qt-5.9.9-gcc13-mingw.patch"
if ! grep -q '^#include <limits>$' "$QT_SOURCE_DIR/src/corelib/tools/qbytearraymatcher.h"; then
    printf '应用 Qt 5.9.9 现代 GCC/MinGW 兼容补丁……\n'
    patch --directory="$QT_SOURCE_DIR" --strip=1 --forward --batch < "$PATCH_FILE"
fi

if [[ ! -f "$QT_BUILD_DIR/Makefile" ]]; then
    printf '配置 32 位静态 Qt……\n'
    (
        cd "$QT_BUILD_DIR"
        "$QT_SOURCE_DIR/configure" \
            -prefix "$QT_INSTALL_DIR" \
            -release -opensource -confirm-license \
            -static -static-runtime \
            -xplatform win32-g++ \
            -device-option CROSS_COMPILE=i686-w64-mingw32- \
            -nomake examples -nomake tests \
            -no-opengl -no-icu -no-harfbuzz -no-freetype \
            -no-libpng -no-libjpeg -no-dbus \
            -qt-zlib -qt-pcre \
            -no-sql-db2 -no-sql-ibase -no-sql-mysql -no-sql-oci \
            -no-sql-odbc -no-sql-psql -no-sql-sqlite \
            -no-sql-sqlite2 -no-sql-tds
    )
fi

printf '构建并安装静态 Qt（JOBS=%s）……\n' "$JOBS"
gmake -C "$QT_BUILD_DIR" -j"$JOBS"
gmake -C "$QT_BUILD_DIR" install

printf '构建 FxPresser 单文件 EXE……\n'
(
    cd "$APP_BUILD_DIR"
    "$QT_INSTALL_DIR/bin/qmake" "$PROJECT_DIR/FxPresser-static.pro"
    gmake -j"$JOBS"
)

APP_EXE="$APP_BUILD_DIR/FxPresser.exe"
if [[ ! -f "$APP_EXE" ]]; then
    printf '构建失败：未生成 %s\n' "$APP_EXE" >&2
    exit 1
fi

install -m 0755 "$APP_EXE" "$OUTPUT_DIR/FxPresser.exe"
(
    cd "$PROJECT_DIR/dist"
    zip -9 -q -FS -r "$(basename "$OUTPUT_ZIP")" "$(basename "$OUTPUT_DIR")"
)

printf '\n构建完成：\n'
file "$OUTPUT_DIR/FxPresser.exe"
sha256sum "$OUTPUT_DIR/FxPresser.exe" "$OUTPUT_ZIP"
printf 'EXE：%s\nZIP：%s\n' "$OUTPUT_DIR/FxPresser.exe" "$OUTPUT_ZIP"
