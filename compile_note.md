# Mixing toolchains across the plugin boundary

Can the host and a plugin be built with different compilers — different GCC
versions, or GCC for the host and Clang for the plugin?

**Measured answer: yes for different GCC versions, and yes for GCC + Clang, as
long as both use the same standard library. GCC/libstdc++ host with a
Clang/libc++ plugin breaks the design in the top level of this repository, and
the cause is one type in the interface rather than anything about `dlopen`.**
The `hardened/` variant fixes it and is verified against that combination.

## The matrix

Host and plugin built separately, then run against each other. Host sources are
`main.cpp host_api.cpp plugin_loader.cpp`; the plugin is `heartbeat_plugin.cpp`.

| Host | Plugin | Result |
| --- | --- | --- |
| g++-16 | g++-16 | works (baseline) |
| g++-16 | g++-12 | works |
| g++-12 | g++-16 | works |
| g++-16 | clang++ 21, libstdc++ | works |
| g++-16 | clang++ 21, **libc++** | `terminate called after throwing an instance of 'std::bad_alloc'` on the first call into the host |
| g++-16 (`hardened/`) | clang++ 21, **libc++** (`hardened/`) | works |

Reproduce with `abi_matrix/run_matrix.sh`.

## Why the compatible combinations work

Everything the boundary relies on is fixed by two specifications that GCC and
Clang both implement on Linux, and that neither breaks between releases:

- **Itanium C++ ABI** — vtable layout and vptr placement, the virtual call
  sequence, `this` passing, name mangling, typeinfo structure.
- **System V psABI** — calling convention, register assignment, small-struct
  parameter passing.

`api_i` is an abstract class whose methods take built-in types, pointers and
references. That is the portable subset, and it is why a virtual interface is a
viable plugin ABI at all.

Note which types stay out of the crossing signatures: `std::map`,
`std::vector`, `std::optional`, `std::filesystem::path`, `std::thread` and
`fmt` are all internal to one image or the other.

Two other choices in the design carry real weight here:

- **Symmetric `plugin_create` / `plugin_destroy`.** In the libc++ run the plugin
  allocates through libc++abi's `operator new` while the host uses libstdc++'s.
  Both allocators coexist in the process without trouble precisely because each
  object is freed by the image that created it. A host that wrote `delete p` on
  a `plugin_i*` would corrupt the heap in that configuration.
- **`noexcept` entry points.** No exception unwinds across the boundary, so the
  two `__cxa_*` implementations never have to agree on typeinfo matching.

Both standard libraries genuinely coexist at runtime:

```console
$ ldd hardened/build/plugins/libheartbeat.so | grep -E 'libc\+\+|libstdc'
	libc++.so.1 => /usr/lib/x86_64-linux-gnu/libc++.so.1
	libc++abi.so.1 => /usr/lib/x86_64-linux-gnu/libc++abi.so.1
$ ldd hardened/build/host | grep -E 'libc\+\+|libstdc'
	libstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6

$ grep -oE 'lib(c\+\+|stdc\+\+)[^ ]*' /proc/<pid>/maps | sort -u
libc++abi.so.1.0
libc++.so.1.0
libstdc++.so.6.0.35
```

## Why libc++ breaks the top-level design: `std::string_view`

`std::string_view` holds the same two members in the opposite order in the two
libraries. `abi_matrix/sv_layout.cpp` dumps the raw words of a
`std::string_view{"abcd", 4}`:

```
libstdc++ : sizeof=16  word0=0x0000000000000004  word1=0x000057ee8ab4b004   (data=0x57ee8ab4b004 size=4)
libc++    : sizeof=16  word0=0x000064d460daa004  word1=0x0000000000000004   (data=0x64d460daa004 size=4)
```

libstdc++ declares `{size_t _M_len; const char* _M_str;}`; libc++ declares
`{const char* __data_; size_type __size_;}`. Both are 16 bytes and trivially
copyable, so both are passed in two registers — and the receiver reads the
pointer as the length.

So when the libc++ plugin calls
`api_->log(LogLevel::Debug, "heartbeat starting")`, the libstdc++ host sees a
length of `0x64d460daa004`, `fmt` tries to allocate about 110 TB, and the
process terminates on `bad_alloc`. It fails on the very first crossing: the
`plugin[DEBUG] heartbeat starting` line never appears. A layout that differed
in a less dramatic way would corrupt memory quietly instead.

The second, smaller hazard is that `enum class E_Status` and
`enum class LogLevel` have no fixed underlying type in the top-level header, so
their width is each compiler's choice. GCC and Clang agree today; fixing the
type costs nothing.

## The fix in `hardened/`: a POD-only boundary

Declare the crossing string type in the ABI header and convert on each side:

```cpp
/// A borrowed character range, laid out by this header rather than by whichever
/// standard library each side happens to use.
struct abi_str {
    const char* data{nullptr};
    std::uint64_t size{0};
};

/// Converts to and from the local standard library's view type. Both are inline,
/// so each image compiles them against its own headers - which is exactly the
/// point.
[[nodiscard]] inline abi_str to_abi(std::string_view text) noexcept {
    return abi_str{text.data(), static_cast<std::uint64_t>(text.size())};
}

[[nodiscard]] inline std::string_view to_sv(abi_str text) noexcept {
    return std::string_view{text.data, static_cast<std::size_t>(text.size)};
}

enum class E_Status : std::uint32_t { Ok, Error, NotFound, AbiMismatch };
enum class LogLevel : std::uint32_t { Debug, Info, Warn, Err };

class PLUGIN_API api_i {
public:
    virtual ~api_i();
    virtual void log(LogLevel level, abi_str msg) noexcept = 0;
    [[nodiscard]] virtual abi_str config(abi_str key) const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t uptime_ms() const noexcept = 0;
    // ...
};
```

Call sites become `api_->log(LogLevel::Debug, to_abi("heartbeat starting"))` and
`to_sv(api_->config(to_abi("heartbeat.period_ms")))`. `kPluginAbiVersion` moves
to 2, since the vtable signatures changed.

Verified output, host built with g++-16/libstdc++ and plugin with
clang++/libc++:

```
[DEBUG] opening plugin ./plugins/libheartbeat.so
[INFO ] loaded plugin 'heartbeat' from ./plugins/libheartbeat.so
[DEBUG] opening plugin ./plugins/libstale.so
[ERR  ] ./plugins/libstale.so was built for ABI 1, host speaks 2: AbiMismatch
[INFO ] plugin[DEBUG] heartbeat starting
[INFO ] 1 plugin(s) running
[INFO ] plugin[INFO] tick at 1 ms
[INFO ] plugin[INFO] tick at 501 ms
[INFO ] plugin[INFO] tick at 1001 ms
[INFO ] shutting down after 1201 ms
[INFO ] plugin[DEBUG] heartbeat thread left
[DEBUG] unloading 1 module(s)
```

The plugin's worker thread calls back into the host across the divide for the
whole run, so the tested path covers more than load and unload.

### Building the cross-toolchain case

```sh
cmake -S hardened -B hardened/build && cmake --build hardened/build -j"$(nproc)"

# replace the heartbeat plugin with a Clang/libc++ build of the same source
clang++ -stdlib=libc++ -std=c++20 -O2 -fvisibility=hidden -fPIC -shared \
        -Ihardened hardened/heartbeat_plugin.cpp \
        -o hardened/build/plugins/libheartbeat.so -lpthread

cd hardened/build && ./host ./plugins
```

## The alternative: detect the mismatch instead of tolerating it

Keeping `std::string_view` in the interface is reasonable when you control every
plugin build. In that case make the handshake reject a foreign toolchain loudly
rather than letting it corrupt memory. Extend the existing version query with a
build-identity tag:

```cpp
#if defined(_LIBCPP_VERSION)
#define PLUGIN_STDLIB_TAG "libc++"
#elif defined(__GLIBCXX__)
#define PLUGIN_STDLIB_TAG "libstdc++/cxx11abi=" PLUGIN_STR(_GLIBCXX_USE_CXX11_ABI)
#else
#define PLUGIN_STDLIB_TAG "unknown"
#endif

/// Identity of the standard library a module was built against. Compared for
/// equality at load time, because the interface passes standard library types.
extern "C" PLUGIN_API const char* plugin_abi_tag() noexcept;
```

The plugin returns `PLUGIN_STDLIB_TAG`; the loader compares it against its own
before calling `plugin_create`, exactly as it already does for
`kPluginAbiVersion`.

Which to choose: the POD boundary when third parties build plugins, the identity
tag when plugins come out of your own CI and ergonomic types are worth more than
toolchain freedom.

## libstdc++ is backward compatible, not forward compatible

Linking the g++-12 host against the distribution's `libfmt.so` failed:

```
libfmt.so: undefined reference to `__cxa_call_terminate@CXXABI_1.3.15'
```

That package was built with a newer GCC. The general rule for any mixed-version
setup: the `libstdc++.so.6` present at link and run time must be at least as new
as the newest toolchain that produced any component. Build a plugin with a newer
GCC than the host's runtime libstdc++ and `dlopen` reports
`version GLIBCXX_3.4.xx not found` — a clean failure, but a failure. Plugins
built with an older GCC load into a host running a newer libstdc++ without
trouble; the reverse does not hold.

The matrix above works around this by building the g++-12 host with
`-DFMT_HEADER_ONLY`, so it never links the newer `libfmt`.

## What may cross the boundary

**Safe:** built-in types; enums with a fixed underlying type; pointers and
references; POD structs declared in the ABI header; abstract classes with
virtual functions; function pointers.

**Unsafe:** any standard library type; `std::shared_ptr` and `std::unique_ptr`;
exceptions; `dynamic_cast` or `catch` on a type owned by the other image;
anything whose size or layout depends on a compile flag set on one side only.

## Toolchains used

Measurements taken on x86-64 Linux/glibc with GCC 12.4 and 16.0.1 (trunk),
Clang 21.1.8, libc++ from LLVM 22, `{fmt}` 10.1.0, and
`libstdc++.so.6.0.35`. The conclusions follow from the Itanium C++ ABI and the
System V psABI rather than from these particular versions, so they carry to
other targets that use both — including the 32-bit ARM toolchains this pattern
is often deployed on.
