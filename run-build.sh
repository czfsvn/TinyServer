#!/usr/bin/env bash
# ============================================================
# TinyServer - 容器内构建脚本
# ============================================================
# 在 cpp-env 容器里执行, 工作目录必须是项目根(即 CMakeLists.txt 的上一层,
# 因为 src/CMakeLists.txt 通过 ../cmake 引用工程级 cmake 模块)。
#
#   bash ./run-build.sh                        # 增量 Debug 构建 -> build/
#   bash ./run-build.sh --clean                # 先删 build 目录再全量构建
#   bash ./run-build.sh --reconfigure          # 保留编译产物, 重跑 cmake configure
#   bash ./run-build.sh --asan                 # 仍用 build/, 只是 -DENABLE_ASAN=ON
#   bash ./run-build.sh --tests --tsan         # 仍用 build/, 开单元测试 + TSAN
#   bash ./run-build.sh -B build-tsan --tests --tsan   # 想并存就自己指定目录
#   bash ./run-build.sh --type Release --target tinyserver
#   bash ./run-build.sh --dry-run              # 只打印将执行的 cmake 命令
#
# 上层封装: scripts/docker-build.sh (Linux 宿主) / scripts/sync.ps1 (Windows)
# ============================================================

set -uo pipefail

# ---------- 默认值 ----------
BUILD_TYPE=Debug
# 固定一个默认构建目录。目录名不随 --tests/--asan/--tsan 变,
# 想并存多份配置(比如同时留一份 tsan)就自己 -B 指定。
BUILD_DIR="build"
JOBS="$(nproc 2>/dev/null || echo 4)"
TARGETS=()
CLEAN=0
RECONFIGURE=0
ENABLE_TESTS=OFF
ASAN=OFF
TSAN=OFF
UBSAN=OFF
ASAN_LEAKS=ON
CMAKE_EXTRA=()
DRY_RUN=0

# ---------- 输出辅助 ----------
if [ -t 1 ]; then
    C_STEP=$'\033[1;36m'; C_OK=$'\033[0;32m'; C_WARN=$'\033[0;33m'
    C_ERR=$'\033[0;31m';  C_OFF=$'\033[0m'
else
    C_STEP=""; C_OK=""; C_WARN=""; C_ERR=""; C_OFF=""
fi
step() { printf '\n%s==> %s%s\n' "$C_STEP" "$*" "$C_OFF"; }
ok()   { printf '%s[ok]%s %s\n'   "$C_OK"   "$C_OFF" "$*"; }
warn() { printf '%s[warn]%s %s\n' "$C_WARN" "$C_OFF" "$*"; }
die()  { printf '%s[fail]%s %s\n' "$C_ERR"  "$C_OFF" "$*" >&2; exit 1; }

usage() {
    cat <<'USAGE'
run-build.sh - build TinyServer inside the cpp-env container

Usage: bash ./run-build.sh [options]

Options:
  -t, --type <T>        Build type: Debug (default) | Release | RelWithDebInfo | MinSizeRel
  -B, --build-dir <dir> Build directory (auto-derived from sanitizer/tests by default)
  -j, --jobs <n>        Parallel jobs (default: nproc)
  -T, --target <name>   Build only this target (repeatable)
  -c, --clean           Remove the build directory first (full rebuild)
  -r, --reconfigure     Keep object files, re-run cmake configure
      --tests           -DENABLE_TESTS=ON
      --asan            -DENABLE_ASAN=ON
      --tsan            -DENABLE_TSAN=ON
      --ubsan           -DENABLE_UBSAN=ON
      --no-leaks        -DENABLE_ASAN_LEAKS=OFF (with --asan)
  -D<var>=<value>       Extra cmake define, passed through verbatim
  -n, --dry-run         Print the cmake commands without running them
  -h, --help            Show this help

Build dir: 'build' by default, no matter which sanitizer/test options are given.
Use -B <dir> to build elsewhere (e.g. keep a -B build-tsan alongside).
Caveat: switching sanitizer/test options inside one dir forces a full rebuild;
the script warns when the existing cache disagrees with what you asked for.
USAGE
    exit 0
}

# ---------- 参数解析 ----------
while [ $# -gt 0 ]; do
    case "$1" in
        -t|--type)        BUILD_TYPE="${2:?--type needs a value}"; shift 2;;
        -B|--build-dir)   BUILD_DIR="${2:?--build-dir needs a value}"; shift 2;;
        -j|--jobs)        JOBS="${2:?--jobs needs a value}"; shift 2;;
        -T|--target)      TARGETS+=("${2:?--target needs a value}"); shift 2;;
        -c|--clean)       CLEAN=1; shift;;
        -r|--reconfigure) RECONFIGURE=1; shift;;
        --tests)          ENABLE_TESTS=ON; shift;;
        --asan)           ASAN=ON; shift;;
        --tsan)           TSAN=ON; shift;;
        --ubsan)          UBSAN=ON; shift;;
        --no-leaks)       ASAN_LEAKS=OFF; shift;;
        -D*)              CMAKE_EXTRA+=("$1"); shift;;
        -n|--dry-run)     DRY_RUN=1; shift;;
        -h|--help)        usage;;
        *)                die "unknown option: $1 (try --help)";;
    esac
done

# ---------- 校验 ----------
[ -f src/CMakeLists.txt ] || die "src/CMakeLists.txt not found - run this from the project root"
case "$BUILD_TYPE" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) die "bad build type: $BUILD_TYPE (want Debug/Release/RelWithDebInfo/MinSizeRel)";;
esac
if [ "$ASAN" = ON ] && [ "$TSAN" = ON ]; then
    die "--asan and --tsan are mutually exclusive (see cmake/compiler_options.cmake)"
fi

# ---------- 构建目录 ----------
# 只用默认的 build/, 目录名不随开关变。想并存多份配置自己 -B。
# 代价: 同一个目录里切换 --tests/--asan/--tsan 就是全量重编。缓存里的开关和
# 本次要的不一样时, 提醒一句 —— 否则"怎么突然编了三分钟"很难自己反应过来。
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    DIFF=""
    for pair in "ENABLE_TESTS:$ENABLE_TESTS" "ENABLE_ASAN:$ASAN" "ENABLE_TSAN:$TSAN" \
                "ENABLE_UBSAN:$UBSAN" "ENABLE_ASAN_LEAKS:$ASAN_LEAKS"; do
        var="${pair%%:*}"
        want="${pair#*:}"
        cur="$(sed -n "s/^${var}:[A-Z]*=\(.*\)$/\1/p" "$BUILD_DIR/CMakeCache.txt" | tail -1)"
        [ -n "$cur" ] && [ "$cur" != "$want" ] && DIFF="$DIFF ${var}:${cur}->${want}"
    done
    if [ -n "$DIFF" ]; then
        warn "cache says:$DIFF - this dir will be reconfigured (full rebuild)."
        warn "use -B <dir> to keep this config in a separate dir"
    fi
fi

# ---------- 组装 cmake 命令 ----------
CFG_ARGS=(cmake -S src -B "$BUILD_DIR"
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
          -DENABLE_TESTS="$ENABLE_TESTS"
          -DENABLE_ASAN="$ASAN"
          -DENABLE_TSAN="$TSAN"
          -DENABLE_UBSAN="$UBSAN"
          -DENABLE_ASAN_LEAKS="$ASAN_LEAKS")
if [ ${#CMAKE_EXTRA[@]} -gt 0 ]; then
    CFG_ARGS+=("${CMAKE_EXTRA[@]}")
fi

BUILD_ARGS=(cmake --build "$BUILD_DIR" --parallel "$JOBS")
if [ ${#TARGETS[@]} -gt 0 ]; then
    BUILD_ARGS+=(--target "${TARGETS[@]}")
fi

run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%s[dry-run]%s %s\n' "$C_WARN" "$C_OFF" "$*"
        return 0
    fi
    "$@"
}

step "TinyServer build"
printf '  type    : %s\n' "$BUILD_TYPE"
printf '  dir     : %s\n' "$BUILD_DIR"
printf '  jobs    : %s\n' "$JOBS"
printf '  tests   : %s\n' "$ENABLE_TESTS"
SANS=""
[ "$ASAN" = ON ]  && SANS="${SANS}asan "
[ "$TSAN" = ON ]  && SANS="${SANS}tsan "
[ "$UBSAN" = ON ] && SANS="${SANS}ubsan "
printf '  sanitizer: %s\n' "${SANS:-none}"
[ ${#TARGETS[@]} -gt 0 ] && printf '  targets : %s\n' "${TARGETS[*]}"

# ---------- clean / reconfigure ----------
if [ "$CLEAN" -eq 1 ]; then
    step "Clean $BUILD_DIR"
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%s[dry-run]%s rm -rf %s\n' "$C_WARN" "$C_OFF" "$BUILD_DIR"
    else
        rm -rf "$BUILD_DIR" || die "failed to remove $BUILD_DIR"
    fi
elif [ "$RECONFIGURE" -eq 1 ] && [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    step "Drop CMakeCache.txt (reconfigure)"
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%s[dry-run]%s rm -f %s/CMakeCache.txt\n' "$C_WARN" "$C_OFF" "$BUILD_DIR"
    else
        rm -f "$BUILD_DIR/CMakeCache.txt" || die "failed to remove CMakeCache.txt"
    fi
fi

# ---------- configure ----------
step "Configure"
T0=$SECONDS
run "${CFG_ARGS[@]}" || die "cmake configure failed"
ok "configure done in $((SECONDS - T0))s"

# ---------- build ----------
step "Build"
T0=$SECONDS
run "${BUILD_ARGS[@]}" || die "build failed - scroll up for the first error"
ok "build done in $((SECONDS - T0))s"

# ---------- 产物 ----------
if [ -d "$BUILD_DIR/bin" ]; then
    step "Artifacts ($BUILD_DIR/bin)"
    find "$BUILD_DIR/bin" -maxdepth 1 -type f -printf '  %f\n' 2>/dev/null | sort
fi
if [ "$ENABLE_TESTS" = ON ]; then
    printf '\nRun tests with: cd %s && ctest --output-on-failure\n' "$BUILD_DIR"
fi

ok "ALL DONE"
