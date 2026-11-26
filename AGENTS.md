# Repository Guidelines

## Project Structure & Module Organization

OS32 is a 32-bit bare-metal OS for NEC PC-9801/9821 machines (x86) and the PowerMac G4 (PowerPC). The tree is split by owner:

- **Common** code lives in `sys/kern/` (including `init/`, `mm/` and `exec/`), `sys/fs/`, `sys/hal/`, `sys/net/`, `sys/input/`, `sys/kapi/`, `sys/include/` and `lib/`.
- **CPU** implementations live in `sys/arch/x86/` and `sys/arch/ppc/`. **Machine** implementations live in `sys/platform/pc98/` (including the V86 monitor in `legacy/`) and `sys/platform/powermac/`.
- **Devices** live in `sys/dev/`, including video backends in `video/` and HWDEV in `hwdev/`. Feature and image configurations live in `sys/conf/`.

CI checks include and link dependencies with `include_layers` and `link_layers`. Common code uses device declarations in `sys/include/<name>_contract.h` and fixed-name interfaces such as `platform_<name>.h` for machine-specific implementations. The fix table is `docs/design/tools/check_layers.md` §8. The `ppc_common_compile` check compiles common sources for PowerPC.
Declare each new source's owner in `build/sources/ownership.mk` and register its build configurations through a source set covered by `OWN_COMPILE_SET` or a per-file `OWN_COMPILE` entry. Verify with `src_owner` and `build_sources`.

See `docs/08_build.md` §8-3 for the tree. `userland/` contains the shell, commands, libraries, and tests. `sdk/` provides application headers, startup code, and Rust bindings. `apps/` and `game/` are independent Git submodules. Assets live in `assets/`; build rules and host tools live in `build/` and `tools/`.

## Build, Test, and Development Commands

Use the WSL toolchain described in [INSTALL.md](INSTALL.md): i386-elf GCC, NASM, Make, Python, and Rust.

- `make all`: build the OS, programs, SDK, and images.
- `make kernel`: build the kernel and SQLite.
- `make programs`: build userland programs.
- apps/game builds are deferred under H-7. Run `git submodule update --init` and `make external` only after the [reopening gate](docs/tasks/v3/T2H_EXTERNAL_GATE.md#再開前に揃える-8-項目) is satisfied and reopening is approved.
- Run one registered check with `python3 -B -m tests.infra.ci run --check <id>`. Add `--mutate` to exercise its registered mutations.
- `make math_test`: build an individual guest test.

Run images in NP21/W. Stop the emulator before `make deploy-kernel`, then restart it. See [docs/08_build.md](docs/08_build.md) for deployment details.

## Worktree Environment (Codex implementers)

Worktrees have no `.env`. Before anything else, export
`CROSS_DIR=/home/hight/opt/cross TMPDIR=/home/hight/os32-tmp/run/<stage> HOST32_RUNNERS=qemu`
(TMPDIR must be on disk; `/tmp` is RAM; native runners hit SIGSYS in the Codex sandbox), then run
`tools/preflight.sh --base <SHA from the request> --fix-fonts` and fix every NG. Build the image requested for the change and run the affected registered checks. Use `--mutate` for checks you add or modify. Once feature development resumes, add a check for the affected contract in the same change.
The PM pushes the candidate to `wt/int-*` and runs `python3 -B tools/ci/land_gate.py <SHA> --evidence <JSON>` before fast-forwarding main. The evidence identifies the successful latest attempt of the same restructure-build run and records both PC-98 and PowerMac boots.
Run checks in the foreground and wait for them to finish instead of re-reading partial logs; never abort and restart a run. Only logs produced after your last source change count as passing.
Never commit (the sandbox keeps `.git` read-only; the PM commits).

## Coding Style & Naming Conventions

Match surrounding formatting; C typically uses four-space indentation, while Make recipes require tabs. Internal C is GNU11 ([C1]): `//`, mid-block declarations, `_Static_assert` (via `STATIC_ASSERT`), `<stdbool.h>` for pure booleans and designated initializers are allowed, but do not mass-rewrite existing code; implicit declarations, implicit int and VLAs are errors. Public SDK headers (`sdk/include/os32/*.h`, including `sdk/include/os32/os32_kapi_shared.h`) must stay C89/GNU89-compatible, and SQLite keeps GNU89. Use `snake_case` functions, uppercase constants, and `libos32*` library names. Use kernel `kstring` helpers instead of libc equivalents. `kprintf` is the kernel diagnostic output and each machine chooses where it goes; call `console_printf` only for output that must reach the terminal (`docs/design/lib/kprintf.md`). Build rules never rename symbols with `-D`.

Never hand-edit generated KAPI files. Follow [docs/KAPI_SPEC.md](docs/KAPI_SPEC.md) for append-only API changes and regeneration; rebuild with `make clean` followed by `make all` after ABI changes.

## Testing Guidelines

Follow the verification stage table in [ROLES §0](docs/tasks/agents/ROLES.md) and report skipped checks.

## Commit & Pull Request Guidelines

Use focused Conventional Commits, matching history: `fix(exec): ...`, `feat(deploy): ...`, or `docs: ...`. For review, describe the problem, resulting behavior, relevant issues, and validation; include screenshots for visible changes.

## Task Playbooks

The step-by-step playbooks live under `.claude/skills/<name>/SKILL.md`. They are plain Markdown; read the one that matches the task before starting.

- `os32-arch-sep`: adding or moving code across the common / CPU / machine / device boundary (layer check, contract headers, `platform_<name>.h`, `Sep-Changes:`).
- `os32-test-basis`: register checks and verify their mutations.
- `os32-kapi-add`: adding or changing a KernelAPI entry.
- `os32-ci-read`: reading CI failures and re-running (PM).
- `os32-ci-inventory`: CI timing records.
- `os32-archive`: summarizing a finished ticket into `docs/design/` before moving it to `docs/archive/` (PM; policy in [docs/POLICY_ARCHIVE.md](docs/POLICY_ARCHIVE.md)).

## Authoritative Guidance

Read [CLAUDE.md](CLAUDE.md) for agent instructions (current team roles: [docs/tasks/agents/ROLES.md](docs/tasks/agents/ROLES.md) §0) and [docs/CONSTRAINTS.md](docs/CONSTRAINTS.md) for mandatory rules. Use [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) to navigate tasks. Update the authoritative document identified in [docs/INDEX.md](docs/INDEX.md), keeping summaries here brief. Never expose `.env` contents or commit the copyrighted `docs/hw/` mirror. [D4]: read or cite `docs/archive/` only when the user (or the PM's request, quoting the user) tells you to — never use archived documents as the basis for work. For new hardware work, follow [HW3]: consult the vendor manual and a BSD/MIT-licensed existing implementation (e.g. FreeBSD), cite them, and never copy code or structure from GPL sources such as Linux.
