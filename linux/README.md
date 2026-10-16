# Building QindieGL on Linux

Cross-compiles the 32-bit Windows `opengl32.dll` with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
so it can be used with Wine/Proton games (e.g. Anachronox via Steam).

```bash
# llvm-mingw (ucrt, x86_64 Linux host) extracted to ~/.local/llvm-mingw
cmake -S . -B build-linux -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686-llvm-mingw.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j8
# -> build-linux/opengl32.dll   (strip with i686-w64-mingw32-strip)
```

Steam launch option: `WINEDLLOVERRIDES="opengl32=n,b" %command%`

Notes
- Shaders are built from source (`SHADERS_FROM_SOURCE=1`), compiled at runtime by d3dx.
- `linux/gen-def.py` generates the export .def: the code is C++-mangled stdcall, `msvc/opengl32.def`
  lists plain GL names. The export table matches the official 1.2.2 build (385 names).
- `QINDIEGL_NO_REMIX=ON` (default) compiles out `idtech3_mixup` (per-game hooks, RTX Remix API).
  That code still uses MSVC inline asm, `__declspec(naked)` and `__FUNCSIG__` and needs porting.
