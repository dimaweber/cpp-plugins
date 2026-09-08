# C++ plugin loader with `dlopen`

A small, complete example of a host application that loads C++ plugins from
shared objects at runtime, hands each one an interface to the host's own
facilities, and unloads them again without crashing at shutdown.

It exists to answer one design question concretely: **what should a plugin's
init function take — `api_i&`, `std::shared_ptr<api_i>`, or `api_i*`?** The
answer here is `api_i&`, and the code shows what that buys across a `dlopen`
boundary.

## The question, and why the reference wins

The host owns the api object. It is constructed before the first plugin loads
and it must still be alive while the last plugin is being torn down. That is a
single-owner, strictly-outlives relationship, and a reference states exactly
that in the type system.

| What the signature says | Idiom |
| --- | --- |
| The plugin uses the api; the host owns it and guarantees it outlives the plugin | `init(api_i&)` |
| The api may be absent | `init(api_i*)`, with a null check on every use |
| The plugin is a co-owner and may be the one that destroys the api | `init(std::shared_ptr<api_i>)` |

Across a DSO boundary the last row carries three specific costs:

- **A `shared_ptr`'s deleter is code, and code gets unmapped.** The control
  block type-erases the deleter, so destruction runs code from whichever image
  created the pointer. Let a `shared_ptr` cross the boundary and `dlclose`
  becomes a lifetime negotiation instead of a decision the host can make on its
  own; a `weak_ptr` extends that window arbitrarily.
- **It drags `libstdc++` into the ABI surface.** The layout of `shared_ptr`, of
  its control block, and the atomics on the refcount all have to match on both
  sides. A plugin built with a different libstdc++, a flipped
  `_GLIBCXX_USE_CXX11_ABI`, or `-static-libstdc++` turns into silent memory
  corruption rather than a link error.
- **It hides the ownership graph.** Every copy silently extends lifetime, which
  is how objects outlive the subsystem that created them.

A reference to an abstract class is one pointer, in a header you control.

## What the example demonstrates

- `api_i&` passed to the plugin, stored as a non-owning `api_i*` member.
- Symmetric `plugin_create` / `plugin_destroy` entry points, so allocation and
  deallocation stay inside one shared object.
- RAII for both resources: `std::unique_ptr<void, DlCloser>` for the `dlopen`
  handle, `std::unique_ptr<plugin_i, PluginDeleter>` for the instance, with the
  deleter carrying the module's own `plugin_destroy` pointer.
- Destruction order encoded in member declaration order, so the instance is
  destroyed while its code is still mapped and `dlclose` runs afterwards.
- An ABI generation check that rejects a stale module before it runs any of its
  own code.
- `-fvisibility=hidden` everywhere with an explicit `PLUGIN_API` export macro,
  so the module's export surface is exactly the contract.
- A plugin that owns a background thread and joins it before unload — the
  single most common source of shutdown crashes in plugin systems.

## Layout

| File | Role |
| --- | --- |
| `plugin_abi.h` | The contract: `api_i`, `plugin_i`, the `extern "C"` entry points, the ABI generation constant, `PLUGIN_API` |
| `host_api.h` / `.cpp` | The host's `api_i` implementation, and `api_i`'s out-of-line destructor |
| `plugin_loader.h` / `.cpp` | `PluginModule` (one loaded `.so` plus its instance) and `PluginRegistry` (loads a directory, drives the group) |
| `main.cpp` | Host entry point; shows the declaration order that makes the api outlive the registry |
| `heartbeat_plugin.cpp` | A plugin with a worker thread, logging through the host api |
| `stale_plugin.cpp` | A module built against an older ABI generation, to exercise the rejection path |
| `example_log.h` | A minimal `fmt`-backed logging stand-in, so the example builds on its own |

## Build and run

Requirements: a C++20 compiler, CMake 3.20+, `{fmt}`, pthreads, and a
`dlopen`-capable libc. Verified with GCC 16 and `{fmt}` 10.1 on Linux/glibc;
the code stays within C++20 as implemented by GCC 10.2, which is the oldest
toolchain it targets.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
cd build && ./host ./plugins
```

Expected output:

```
[DEBUG] opening plugin ./plugins/libheartbeat.so
[INFO ] loaded plugin 'heartbeat' from ./plugins/libheartbeat.so
[DEBUG] opening plugin ./plugins/libstale.so
[ERR  ] ./plugins/libstale.so was built for ABI 0, host speaks 1: AbiMismatch
[INFO ] plugin[DEBUG] heartbeat starting
[INFO ] 1 plugin(s) running
[INFO ] plugin[INFO] tick at 2 ms
[INFO ] plugin[INFO] tick at 502 ms
[INFO ] plugin[INFO] tick at 1002 ms
[INFO ] shutting down after 1202 ms
[INFO ] plugin[DEBUG] heartbeat thread left
[DEBUG] unloading 1 module(s)
```

The export surface of a built module is exactly the three entry points plus the
interface typeinfo:

```console
$ nm -D --defined-only build/plugins/libheartbeat.so | grep -v ' [wWbB] ' | c++filt
0000000000001f10 T plugin_abi_version
0000000000001f20 T plugin_create
0000000000001f60 T plugin_destroy
0000000000004db0 V typeinfo for plugin_i
0000000000003050 V typeinfo name for plugin_i
```

## The boundary contract

`api_i`'s declaration documents the two promises a plugin author needs:

- **Lifetime.** The api instance outlives every plugin. The host destroys all
  plugin instances and `dlclose`s their modules before tearing the api down. A
  plugin may cache the reference for its whole life, including on background
  threads, provided it joins those threads in its destructor.
- **Thread safety.** Every `api_i` method is callable concurrently from any
  thread. Plugin authors guess wrong when this goes unsaid.

The entry points are `noexcept`. An exception unwinding across the boundary
depends on both images sharing one libstdc++ and one set of typeinfo symbols;
status codes depend on nothing. `plugin_create` uses
`new (std::nothrow)` so an allocation failure returns `nullptr` instead of
escaping as an exception.

## Four rules that make it correct

**1. Destruction order lives in declaration order.** Members are destroyed in
reverse declaration order, so `PluginModule` declares `handle_` before
`instance_`: the instance dies while its code is still mapped, and `dlclose`
runs after. `main` declares `api` before `registry` for the same reason. Get
this backwards and you get a crash inside a virtual call that reproduces only at
shutdown.

**2. Join every thread before unload.** `HeartbeatPlugin::run()` is code in the
`.so`. A thread still runnable when `dlclose` unmaps it kills the process at
whatever instruction it reached, with a backtrace pointing at nothing. The join
sits in the destructor, not only in `stop()`, so it holds on every path. The
same applies to `atexit` handlers, timer callbacks, and threads a plugin starts
indirectly.

**3. Allocate and free in the same image.** `plugin_create` and
`plugin_destroy` are symmetric, and `PluginDeleter` carries the module's own
`destroy` pointer, so the host never writes `delete p` on a `plugin_i*`. This is
what lets a plugin ship its own allocator or a statically linked libstdc++.

**4. Version the ABI, and check it first.** `plugin_abi_version()` is queried
before `plugin_create`, so a mismatched module never runs its own construction
code. Bump `kPluginAbiVersion` on any change to `api_i` or `plugin_i` —
*including adding a virtual function*, which reorders the vtable and makes an
old plugin call a different slot than it was compiled against.

## Notes on visibility and RTTI

Both sides build with `-fvisibility=hidden`, and `PLUGIN_API`
(`__attribute__((visibility("default")))`) marks what the other side must see.
`api_i`'s destructor is defined out-of-line in one host translation unit, which
pins its vtable and typeinfo to the host image instead of leaving weak copies in
each DSO for the dynamic linker to merge.

The host links with `ENABLE_EXPORTS ON` (`-rdynamic`), putting its own symbols
in the dynamic symbol table. A plugin that only calls virtuals through the
reference resolves nothing from the host and runs without it; it becomes
necessary as soon as a plugin `dynamic_cast`s or catches an interface type
across the boundary, which is the failure mode that presents as a `dynamic_cast`
returning `nullptr` for an object that plainly has the right type.

## `dlopen` flags used

- `RTLD_NOW` — an unresolved symbol surfaces at load time rather than at the
  first call into it.
- `RTLD_LOCAL` — the module's symbols stay out of the global namespace, so
  plugins cannot shadow each other. `RTLD_GLOBAL` is for plugins that
  deliberately share symbols, and brings first-loaded-wins surprises when they
  do not.
- `RTLD_NODELETE` is worth adding under valgrind or ASan: it keeps the module
  mapped so leak reports retain symbol names, at the cost of a real unload.

`dlsym` returns `nullptr` both for an absent symbol and for one whose value is
`nullptr`, so `dl_symbol()` clears `dlerror()` first and reads it afterwards.
The `reinterpret_cast` from `void*` to a function pointer is conditionally
supported by the standard and guaranteed by POSIX.

## Adapting it

The example is deliberately self-contained. In a real tree:

- Replace `example_log.h` with the project's logging header.
- Return a rich result type (`std::expected`, or an in-house `result_t<T>`) from
  `PluginModule::load` instead of `std::optional`, so the caller sees the reason
  the way the log already does.
- Give the host a stable plugin directory and a naming convention, and consider
  putting the ABI generation in the filename so an incompatible module is
  visible before it is opened.

## License

Pick one before publishing — this is example code intended to be copied.
