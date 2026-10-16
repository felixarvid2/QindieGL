# Working in this repository (QindieGL fork)

C++ OpenGL-to-D3D9 wrapper (`code/`, `idtech3_mixup/`, `extern/`). The fork adds a Linux cross-build
(`linux/`, `CMakeLists.txt`) so the Windows DLL can be used with Wine/Proton.

## Who does what

Subagents live in `.claude/agents/` (copied from felixarvid2/test).

| Work | Who | Model |
|---|---|---|
| Finding files and symbols, reading code, answering "where/how" | `Explore` | Haiku |
| Simple mechanical edits that are already fully specified | `mechanic` | Haiku |
| Regular coding: implementing a specified feature or fix | `implementer` | Sonnet |
| Architecture, trade-offs, hard bugs with an unknown cause | the main session, or `architect` | Opus |

- Delegate search and reading to `Explore`; pass it the question, not a file list.
- Hand `implementer` a clear spec (what, where, how to verify); hand `mechanic` the exact change.
  Run independent pieces in parallel.
- Keep decisions in the main session and check subagent results (diff + build output) before committing.
- Nothing is done until the Linux build passes:
  `cmake -S . -B build-linux -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686-llvm-mingw.cmake && cmake --build build-linux -j8`
