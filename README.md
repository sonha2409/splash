# splash

A novel Unix shell written in C. Daily-driver capable with structured data pipes, fish-style line editing, and progressive POSIX compatibility.

## Features

- **Structured data pipes** — `|>` operator passes typed data (tables, lists, values) between commands with lazy evaluation
- **Fish-style line editor** — syntax highlighting, autosuggestions, and tab completion as you type
- **Hand-rolled parser** — recursive descent with incremental parsing for real-time highlighting
- **Job control** — full `fg`, `bg`, `jobs` support with proper signal handling
- **Typed value system** — tagged union supporting strings, ints, floats, bools, tables, lists, and nil
- **POSIX compatible** — pipes, redirects, globs, environment variables, and command substitution

## Building

Requires a C17 compiler (tested with Apple Clang on macOS).

```sh
make          # release build
make debug    # debug build with sanitizers
make test     # run all tests
make clean    # remove build artifacts
```

## Usage

```sh
./splash
```

## Project Structure

```
src/
├── main.c          # Entry point, REPL loop
├── tokenizer.c/h   # Lexical analysis
├── parser.c/h      # Recursive descent parser
├── command.c/h     # Command data structures (AST)
├── executor.c/h    # fork/exec/pipe/redirect
├── builtins.c/h    # Built-in commands (cd, exit, etc.)
├── jobs.c/h        # Job control
├── signals.c/h     # Signal handlers
├── expand.c/h      # Variable/tilde/wildcard expansion
├── editor.c/h      # Line editor
├── history.c/h     # Command history
├── highlight.c/h   # Syntax highlighting
├── complete.c/h    # Tab completion
├── value.c/h       # Tagged union Value type
├── table.c/h       # Structured data tables
├── pipeline.c/h    # Structured pipe evaluation
├── config.c/h      # Configuration loading
└── util.c/h        # String helpers, memory utils
tests/
├── test_*.c         # Unit tests
├── integration/     # End-to-end shell script tests
└── fuzz/            # Fuzz test harnesses
```

## License

All rights reserved.
