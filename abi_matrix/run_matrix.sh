#!/usr/bin/env bash
# Builds the host and the plugin with different toolchains and runs each pairing.
# See ../compile_note.md for what the results mean.
set -u
cd "$(dirname "$0")/.." || exit 1
OUT=$PWD/abi_matrix
HOST_SRC="main.cpp host_api.cpp plugin_loader.cpp"
COMMON="-std=c++20 -O2 -fvisibility=hidden -fPIC -I."

build_host() { # $1=compiler $2=tag $3=extra flags (optional)
    local cc=$1 tag=$2 extra=${3:-} libs="-ldl -lpthread"
    command -v "$cc" > /dev/null || return 1
    mkdir -p "$OUT/$tag" || return 1
    local s
    for s in $HOST_SRC; do
        $cc $COMMON -fvisibility-inlines-hidden $extra -c "$s" -o "$OUT/$tag/${s%.cpp}.o" || return 1
    done
    case "$extra" in
        *FMT_HEADER_ONLY*) ;;
        *) libs="-lfmt $libs" ;;
    esac
    $cc -o "$OUT/$tag/host" "$OUT/$tag"/*.o -rdynamic $libs
}

build_plugin() { # $1=compiler $2=tag $3=extra flags (optional)
    local cc=$1 tag=$2 extra=${3:-}
    command -v "$cc" > /dev/null || return 1
    mkdir -p "$OUT/$tag/plugins" || return 1
    $cc $COMMON $extra -shared heartbeat_plugin.cpp -o "$OUT/$tag/plugins/libheartbeat.so" -lpthread
}

run() { # $1=host tag $2=plugin tag $3=label
    printf '\n### %s\n' "$3"
    if [ ! -x "$OUT/$1/host" ] || [ ! -f "$OUT/$2/plugins/libheartbeat.so" ]; then
        echo "    (skipped: toolchain unavailable)"
        return
    fi
    timeout 20 "$OUT/$1/host" "$OUT/$2/plugins" 2>&1 | sed 's/^/    /'
}

echo "### std::string_view layout, raw words of string_view{\"abcd\", 4}"
g++ -std=c++20 -O2 "$OUT/sv_layout.cpp" -o "$OUT/sv_probe" 2> /dev/null &&
    printf '    %-24s' 'g++ / libstdc++:' && "$OUT/sv_probe"
clang++ -stdlib=libc++ -std=c++20 -O2 "$OUT/sv_layout.cpp" -o "$OUT/sv_probe" 2> /dev/null &&
    printf '    %-24s' 'clang++ / libc++:' && "$OUT/sv_probe"

echo
# The distribution libfmt may need a newer CXXABI than g++-12 provides, so that
# host is built header-only.
build_host g++-16 h16 && echo "built host g++-16"
build_host g++-12 h12 "-DFMT_HEADER_ONLY" && echo "built host g++-12 (fmt header-only)"
build_plugin g++-16 p_gcc16 && echo "built plugin g++-16"
build_plugin g++-12 p_gcc12 && echo "built plugin g++-12"
build_plugin clang++ p_clang_libstdcxx && echo "built plugin clang/libstdc++"
build_plugin clang++ p_clang_libcxx "-stdlib=libc++" && echo "built plugin clang/libc++"

run h16 p_gcc16           "host g++-16 + plugin g++-16 (baseline)"
run h16 p_gcc12           "host g++-16 + plugin g++-12"
run h12 p_gcc16           "host g++-12 + plugin g++-16"
run h16 p_clang_libstdcxx "host g++-16 + plugin clang/libstdc++"
run h16 p_clang_libcxx    "host g++-16 + plugin clang/libc++   <-- expected to fail"
