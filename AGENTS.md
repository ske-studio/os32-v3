# Repository Guidelines

## Project Structure & Module Organization

OS32 is a 32-bit bare-metal OS for NEC PC-9801/9821 machines. Kernel code lives in `kernel/`, `exec/`, `drivers/`, `fs/`, and `gfx/`; shared headers and utilities live in `include/` and `lib/`. `userland/` contains the shell, commands, libraries, and tests. `sdk/` provides application headers, startup code, and Rust bindings. `apps/` and `game/` are independent Git submodules. Assets live in `assets/`; build rules and host tools live in `build/` and `tools/`.

## Build, Test, and Development Commands

Use the WSL toolchain described in [INSTALL.md](INSTALL.md): i386-elf GCC, NASM, Make, Python, and Rust.

- `make all`: build the OS, programs, SDK, and images.
- `make kernel`: build the kernel and SQLite.
- `make programs`: build userland programs.
- `git submodule update --init` then `make external`: initialize and build apps and game.
- `make check-fast` / `make check-changed` / `make check`: host tests and repository checks (KAPI versions, manifests, constraint IDs, privileged instructions, docs links / orphans / status-line vocabulary) — without mutants / with mutants only for what you changed / with all mutants (before merging). Stages and times: [docs/08_build.md](docs/08_build.md) §8-4.
- `make math_test`: build an individual guest test.

Run images in NP21/W. Stop the emulator before `make deploy-kernel`, then restart it. See [docs/08_build.md](docs/08_build.md) for deployment details.

## Coding Style & Naming Conventions

Match surrounding formatting; C typically uses four-space indentation, while Make recipes require tabs. Internal C is GNU11 ([C1]): `//`, mid-block declarations, `_Static_assert` (via `STATIC_ASSERT`), `<stdbool.h>` for pure booleans and designated initializers are allowed, but do not mass-rewrite existing code; implicit declarations, implicit int and VLAs are errors. Public SDK headers (`sdk/include/os32/*.h`, including `sdk/include/os32/os32_kapi_shared.h`) must stay C89/GNU89-compatible, and SQLite keeps GNU89. Use `snake_case` functions, uppercase constants, and `libos32*` library names. Use kernel `kstring` helpers instead of libc equivalents.

Never hand-edit generated KAPI files. Follow [docs/KAPI_SPEC.md](docs/KAPI_SPEC.md) for append-only API changes and regeneration; rebuild with `make clean` followed by `make all` after ABI changes.

## Testing Guidelines

Tests use custom guest programs, commonly `userland/tests/<name>_test.c`, and boot-time checks in `kernel/kselftest.c`. Build and deploy relevant tests, run them in NP21/W, and record results. Extend kernel selftests when changing primitives. No percentage coverage target is specified. Verify the deployed binary: HostDrv deployment alone may leave an older NHD binary running. Report skipped checks explicitly.

## Commit & Pull Request Guidelines

Use focused Conventional Commits, matching history: `fix(exec): ...`, `feat(deploy): ...`, or `docs: ...`. For review, describe the problem, resulting behavior, relevant issues, and validation; include screenshots for visible changes.

## Authoritative Guidance

Read [CLAUDE.md](CLAUDE.md) for agent instructions (current team roles: [docs/tasks/agents/ROLES.md](docs/tasks/agents/ROLES.md) §0) and [docs/CONSTRAINTS.md](docs/CONSTRAINTS.md) for mandatory rules. Use [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) to navigate tasks. Update the authoritative document identified in [docs/INDEX.md](docs/INDEX.md), keeping summaries here brief. Never expose `.env` contents or commit the copyrighted `docs/hw/` mirror.
