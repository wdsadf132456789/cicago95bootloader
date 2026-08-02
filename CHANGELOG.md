# Changelog

All notable changes to the Chicago-95 / BrainFS Bootloader are tracked here.
Format follows [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [0.1.2-Alpha] - 2026-08-02

### Added
- Kernel shell memory-stream piping support (`|`) for chaining commands (e.g. `ps | grep R0`).
- Built-in text processing & filtering commands in Ring-0 kernel shell: `grep`, `head`, `tail`, `wc`.
- Full VFS file navigation and management commands: `pwd`, `cd`, `ls`, `cat`, `touch`, `mkdir`, `rm`, `write`, `echo`, `stat`.
- Kernel console output redirection support (`>` and `>>`) to VFS files.
- `uptime` kernel shell command displaying ticks, formatted uptime, and timer frequency.

### Changed
- Prompts now show active working directory (`chicago-95:<cwd>> `).
- Version bumped from `0.1.1-beta` to `0.1.2-Alpha` across README, kernel shell, and stage2 shell.

## [0.1.1-beta] - 2026-08-01

### Added
- `hyfetch` kernel shell command: same system info as `neofetch` but with the
  HyFetch pride-flag (rainbow) gradient on the Chicago skyline and footer bar.
- Interactive kernel build prompt (`make` now asks whether to compile the
  kernel, with a confirmation step) and a full CMake build system that
  reproduces the Makefile output byte-for-byte.

### Changed
- **CMake is now required before `make`.** Running `make` without configuring
  CMake first refuses to build and points the user at `cmake -S . -B build-cmake`
  (or `cmake .`). The original hand-written build is preserved as
  `Makefile.original`.
- Version bumped to 0.1.1-beta (README, kernel shell, stage-2 shell).
- Build instructions reorganized in README: CMake flow first, Makefile kept as
  the original/optional path.
- Removed an unused `cyan` variable in the kernel shell's fetch display.

## [0.1.0-beta]

### Added
- 100-stage x86_64 bare-metal boot chain (MBR -> stage 2 -> ELF64 loader ->
  kernel) with a BETA warning banner.
- Kernel: shell, dmesg ring buffer, btop monitor, process manager, syscalls,
  VFS, PCI/ATA/NVMe/xHCI/e1000/USB drivers, network stack.
- Boot manager (stages 5-100) with 61+ demo templates and a Winamp-style
  PC-speaker music player.
- `neofetch` shell command with ASCII Chicago skyline.
