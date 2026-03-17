# Architecture Overview

splash is a Unix shell written in C17, targeting macOS (Darwin).

## Pipeline

```
Input → Tokenizer → Parser → AST (Command structs) → Executor → Output
```

The shell operates as a REPL loop:
1. Print prompt
2. Read a line of input
3. Tokenize into a token stream
4. Parse tokens into an AST
5. Execute the AST (fork/exec for external commands, direct calls for builtins)
6. Repeat

## Directory Structure

- `src/` — all source code, one `.c`/`.h` pair per module
- `tests/` — unit tests (`test_*.c`), integration tests (`integration/`), fuzz harnesses (`fuzz/`)
- `docs/` — architecture and per-feature design documents
- `build/` — compiled object files (not committed)

## Build System

The Makefile provides:
- `make` — release build with optimizations
- `make debug` — debug build with ASan + UBSan (leak sanitizer not available on macOS ARM; ASan covers most leak detection)
- `make test` — compile and run all unit tests
- `make clean` — remove build artifacts

## Memory Management

Manual malloc/free with strict ownership conventions:
- `xmalloc`, `xcalloc`, `xrealloc`, `xstrdup` wrappers abort on allocation failure
- Ownership is documented in function comments
- Debug builds run under AddressSanitizer and UndefinedBehaviorSanitizer
