#!/bin/bash
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# IMPORTANT! Before running this script you have to run
# `rm -r ~/scratch && mkdir ~/scratch` first
#
#
# For more fine-grained instructions, see:
# https://docs.google.com/document/d/1chTvr3fSofQNV_PDPEHRyUgcJCQBgTDOOBriW9gIm9M/edit?ts=5e9549a2#heading=h.fjdnrdg1gcty

set -e  # makes the script quit on any command failure
set -u  # unset variables are quit-worthy errors

PLATFORMS="linux,android,ash,cros,win"
if [ "$1" != "" ]
then
  PLATFORMS="$1"
fi

COMPILE_DIRS=.
EDIT_DIRS=.

# Save llvm-build as it is about to be overwritten.
mv third_party/llvm-build third_party/llvm-build-upstream

# Build and test the rewriter.
echo "*** Building the rewriter ***"
time tools/clang/scripts/build.py \
    --with-android \
    --without-fuchsia \
    --extra-tools rewrite_templated_container_fields
tools/clang/rewrite_templated_container_fields/tests/run_all_tests.py

args_for_platform() {
    case "$1" in

    android)
        cat <<EOF
target_os = "android"
clang_use_chrome_plugins = false
is_chrome_branded = true
is_debug = false
dcheck_always_on = true
is_official_build = true
symbol_level = 1
use_remoteexec = false
enable_remoting = true
enable_webview_bundles = true
ffmpeg_branding = "Chrome"
proprietary_codecs = true
force_enable_raw_ptr_exclusion = true
EOF
        ;;

    win)
        cat <<EOF
target_os = "win"
clang_use_chrome_plugins = false
enable_precompiled_headers = false
is_chrome_branded = true
is_debug = false
dcheck_always_on = true
is_official_build = true
symbol_level = 1
use_remoteexec = false
chrome_pgo_phase = 0
force_enable_raw_ptr_exclusion = true
EOF
        ;;

    linux)
        cat <<EOF
target_os = "linux"
clang_use_chrome_plugins = false
dcheck_always_on = true
is_chrome_branded = true
is_debug = false
is_official_build = true
use_remoteexec = false
chrome_pgo_phase = 0
force_enable_raw_ptr_exclusion = true
EOF
        ;;

    cros)
        cat <<EOF
target_os = "chromeos"
clang_use_chrome_plugins = false
chromeos_is_browser_only = true
dcheck_always_on = true
is_chrome_branded = true
is_debug = false
is_official_build = true
use_remoteexec = false
chrome_pgo_phase = 0
force_enable_raw_ptr_exclusion = true
EOF
        ;;

    ash)
        cat <<EOF
target_os = "chromeos"
clang_use_chrome_plugins = false
dcheck_always_on = true
is_debug = false
is_official_build = true
use_remoteexec = false
chrome_pgo_phase = 0
force_enable_raw_ptr_exclusion = true
EOF
        ;;

    mac)
        cat <<EOF
target_os = "mac"
clang_use_chrome_plugins = false
dcheck_always_on = true
is_chrome_branded = true
is_debug = false
is_official_build = true
use_remoteexec = false
chrome_pgo_phase = 0
symbol_level = 1
force_enable_raw_ptr_exclusion = true
EOF
        ;;

    *)
        echo "unknown platform"
        exit 1
        ;;
    esac
}

pre_process() {
    PLATFORM="$1"
    OUT_DIR="out/rewrite-$PLATFORM"

    mkdir -p "$OUT_DIR"
    args_for_platform "$PLATFORM" > "$OUT_DIR/args.gn"

    # Build generated files that a successful compilation depends on.
    echo "*** Preparing targets for $PLATFORM ***"
    gn gen $OUT_DIR
    time ninja -C $OUT_DIR -t targets all \
        | grep '^gen/.*\(\.h\|inc\|css_tokenizer_codepoints.cc\)' \
        | cut -d : -f 1 \
        | xargs -s $(expr $(getconf ARG_MAX) - 256) ninja -C $OUT_DIR

    TARGET_OS_OPTION=""
    if [ $PLATFORM = "win" ]; then
        TARGET_OS_OPTION="--target_os=win"
    fi
}

main_rewrite() {
    PLATFORM=$1
    OUT_DIR="out/rewrite-${PLATFORM}"

    TARGET_OS_OPTION=""
    if [ $PLATFORM = "win" ]; then
        TARGET_OS_OPTION="--target_os=win"
    fi

    # Main rewrite.
    echo "*** Running the main rewrite phase for $PLATFORM ***"
    time tools/clang/scripts/run_tool.py \
        $TARGET_OS_OPTION \
        --tool rewrite_templated_container_fields \
        --generate-compdb \
        -p $OUT_DIR \
        $COMPILE_DIRS > ~/scratch/rewriter-$PLATFORM.main.out
    cat ~/scratch/rewriter-$PLATFORM.main.out >> ~/scratch/rewriter.main.out
}

for PLATFORM in ${PLATFORMS//,/ }
do
    pre_process "$PLATFORM"
done

for PLATFORM in ${PLATFORMS//,/ }
do
    main_rewrite "$PLATFORM"
done

# Apply edits generated by the main rewrite.
echo "*** Applying edits ***"
cat ~/scratch/rewriter.main.out | \
    tools/clang/rewrite_templated_container_fields/extract_edits.py | \
    tools/clang/scripts/apply_edits.py -p out/rewrite-win $EDIT_DIRS

# Format sources, as many lines are likely over 80 chars now.
echo "*** Formatting ***"
time git cl format

# Restore llvm-build. Without this, your future builds will be painfully slow.
rm -r -f third_party/llvm-build
mv third_party/llvm-build-upstream third_party/llvm-build
