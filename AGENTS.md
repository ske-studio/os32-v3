# Repository Guidelines

## Project Structure & Module Organization

OS32 is a 32-bit bare-metal OS for NEC PC-9801/9821 machines. Kernel code lives in `kernel/`, `exec/`, `drivers/`, `fs/`, and `gfx/`; shared headers and utilities live in `include/` and `lib/`. `userland/` contains the shell, commands, libraries, and tests. `sdk/` provides application headers, startup code, and Rust bindings. `apps/` and `game/` are independent Git submodules. Assets live in `assets/`; build rules and host tools live in `build/` and `tools/`.

## Build, Test, and Development Commands

Use the WSL toolchain described in [INSTALL.md](INSTALL.md): i386-elf GCC, NASM, Make, Python, and Rust.

- `make all`: build the OS, programs, SDK, and images.
- `make kernel`: build the kernel and SQLite.
- `make programs`: build userland programs.
- apps/game builds are deferred under H-7. Run `git submodule update --init` and `make external` only after the [reopening gate](docs/tasks/v3/T2H_EXTERNAL_GATE.md#再開前に揃える-8-項目) is satisfied and reopening is approved.
- `make check-fast` / `make check-changed` / `make check`: host tests and repository checks (KAPI versions, manifests, constraint IDs, privileged instructions, docs links / orphans / status-line vocabulary) — without mutants / with mutants only for what you changed / with all mutants (before merging). Stages and times: [docs/08_build.md](docs/08_build.md) §8-4.
- `make math_test`: build an individual guest test.

Run images in NP21/W. Stop the emulator before `make deploy-kernel`, then restart it. See [docs/08_build.md](docs/08_build.md) for deployment details.

## Worktree Environment (Codex implementers)

Worktrees have no `.env`. Before anything else, export
`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp/run/<stage> HOST32_RUNNERS=qemu`
(TMPDIR must be on disk; `/tmp` is RAM; native runners hit SIGSYS in the Codex sandbox), then run
`tools/preflight.sh --base <SHA from the request> --fix-fonts` and fix every NG. Run only `make all` and the tests you added or touched, with `MUTATE=0`; also run
`python3 tools/check_select.py --lint` (seconds; register new test inputs in `tools/check_map.d/`). Do not run
mutation passes, the full `--select` candidate set or the static checks (the PM runs them once after review).
For a request that is one step of a multi-step task (Tx, from T4): run `make kernel` (plus any userland image your step touches) instead of `make all`; after the last step, before review, run `make all`, the `check_select.py --select --files <whole Tx diff>` candidates with `MUTATE=0`, and the five static checks once (ROLES §0).
Run checks in the foreground and wait for them to finish instead of re-reading partial logs; never abort and restart a run. Only logs produced after your last source change count as passing.
Never commit (the sandbox keeps `.git` read-only; the PM commits).

## Coding Style & Naming Conventions

Match surrounding formatting; C typically uses four-space indentation, while Make recipes require tabs. Internal C is GNU11 ([C1]): `//`, mid-block declarations, `_Static_assert` (via `STATIC_ASSERT`), `<stdbool.h>` for pure booleans and designated initializers are allowed, but do not mass-rewrite existing code; implicit declarations, implicit int and VLAs are errors. Public SDK headers (`sdk/include/os32/*.h`, including `sdk/include/os32/os32_kapi_shared.h`) must stay C89/GNU89-compatible, and SQLite keeps GNU89. Use `snake_case` functions, uppercase constants, and `libos32*` library names. Use kernel `kstring` helpers instead of libc equivalents.

Never hand-edit generated KAPI files. Follow [docs/KAPI_SPEC.md](docs/KAPI_SPEC.md) for append-only API changes and regeneration; rebuild with `make clean` followed by `make all` after ABI changes.

## Testing Guidelines

Tests use custom guest programs, commonly `userland/tests/<name>_test.c`, and boot-time checks in `kernel/kselftest.c`. Build and deploy relevant tests, run them in NP21/W, and record results. Extend kernel selftests when changing primitives. No percentage coverage target is specified. Verify the deployed binary: HostDrv deployment alone may leave an older NHD binary running. Report skipped checks explicitly.

## Commit & Pull Request Guidelines

Use focused Conventional Commits, matching history: `fix(exec): ...`, `feat(deploy): ...`, or `docs: ...`. For review, describe the problem, resulting behavior, relevant issues, and validation; include screenshots for visible changes.

## Authoritative Guidance

Read [CLAUDE.md](CLAUDE.md) for agent instructions (current team roles: [docs/tasks/agents/ROLES.md](docs/tasks/agents/ROLES.md) §0) and [docs/CONSTRAINTS.md](docs/CONSTRAINTS.md) for mandatory rules. Use [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) to navigate tasks. Update the authoritative document identified in [docs/INDEX.md](docs/INDEX.md), keeping summaries here brief. Never expose `.env` contents or commit the copyrighted `docs/hw/` mirror. For new hardware work, follow [HW3]: consult the vendor manual and a BSD/MIT-licensed existing implementation (e.g. FreeBSD), cite them, and never copy code or structure from GPL sources such as Linux.
