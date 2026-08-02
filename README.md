# Chicago-95 / BrainFS Bootloader

> **WARNING: WORK IS IN BETA USE IT AT YOUR OWN COST NO CHARGES CAN'T BE PRESSED TO THE OWNER OF THIS PROJECT DUE TO BROKEN DEVICES, BOOTLOADER PROBLEMS OR BIOS NOT WORKING**
> **YOU HAVE BEEN WARNED**
>
> Status: BETA - Not for production use.
>
> This software is currently in beta. Expect bugs, incomplete features, and breaking changes between versions. Do not use this in any production or mission-critical environment.

A from-scratch x86_64 bootloader with an integrated kernel, security suite, and shell — all running bare-metal before any OS loads. No libc, no operating system, no compromise.

**Status:** BETA — Not for production use

**Version:** 0.1.2-Alpha | **Lines:** ~1.4 million | **Target:** x86_64 ring-0 bare-metal

## Build

**CMake is required** — you must run `cmake` before `make`. Running `make`
without configuring CMake first refuses to build.

**Out-of-source (recommended):**

```bash
cmake -S . -B build-cmake   # configure once
make                        # full build; after the stages it asks:
                            #   "Would you like to compile the kernel now? Y [Yes] N [No]"
                            #   then confirms with "Are you sure you want to (not) compile
                            #   the kernel?" before building/skipping
```

**In-source:**

```bash
cmake .                     # configure once (replaces this Makefile with CMake's generated one)
make
```

**Makefile (original, not required):** the hand-written build is preserved as
`Makefile.original` and still works standalone:

```bash
make -f Makefile.original && make -f Makefile.original all  # Bootloader
cd kernel && make clean && make                             # Kernel
```

**Toolchain:** `gcc -m32` (stage 2), `gcc -m64 -mcmodel=kernel` (kernel), `nasm -f bin`, `cmake` (required), `python3`, `dd`, `truncate`

The CMake build detects the build machine's PCI devices via `tools/scan_driver.c` and only compiles the wifi/optional drivers that are actually needed — exactly like `make scan && make` in the Makefile flow.

## Boot Chain

```
BIOS
 └─ Stage 1: MBR (512 bytes, 0x7C00 → reloc 0x0600)
     └─ Stage 2: Real → Protected → Long mode
         ├─ E820 memory map (parsed at 0x8000)
         ├─ 7-phase pre-init hardening (see below)
         ├─ 55 security modules initialized
         ├─ Fish shell + GUI
         └─ Stages 3–100 (boot manager menu)
             ├─ Stage 3: ELF64 loader (higher-half aware)
             ├─ Stage 4: Kernel at 0xFFFFFFFF80000000
             ├─ Stages 5–8: Not currently used
             └─ Stages 9–100: 61 unique demo templates
```

## Boot-Time Stages (9–100)

92 boot-time stages generated from 61 unique templates, cycling every full rotation. Each stage runs as an independent 64-bit binary loaded from disk by the stage2 boot manager. Every stage has full VGA text-mode output (80×25 at 0xB8000), keyboard input, and a delay loop.

| Category | Templates | Stages |
|----------|-----------|--------|
| **Classic** (22) | bounce, colors, count, scroll, beep, stars, border, wave, bars, fib, primes, pong, maze, spiral, snow, fire, matrix, life, clock, noise, collatz | 9–30, 67–88 |
| **Language Demos** (36) | awk, php, js, ruby, python, rust, go, lisp, sql, haskell, brainfuck, lua, cpp, bash, perl, typescript, kotlin, swift, dart, csharp, forth, prolog, cobol, fortran, julia, zig, apl, erlang, elixir, clojure, vhdl, r, ada, logo, smalltalk, assembly, arch | 31–66, 89–100 |
| **Player** (2) | bootlogo, mp3 | 68–69 |

Each stage shows syntax, code patterns, and visual effects inspired by its language — from Brainfuck tape visualization to SQL table results to an Arch Linux `pacman -Syu` simulation.

## Pre-Init Hardening (7 Phases)

Runs before anything else in Stage 2. Direct VGA output at 0xB8000.

| Phase | Name | What It Does |
|-------|------|--------------|
| PRE-01 | CPU Fingerprint | CPUID leaves 0,1,2,7,0x80000002-6 — vendor, features, cache, XCR0/AVX state |
| PRE-02 | Memory Fingerprint | Parse E820, detect overlaps (O(n²) check), CRC32 of table, flag sane memory >1MB |
| PRE-03 | RNG Seed | 256-bit entropy: TSC jitter (256 samples XOR-folded) + I/O port reads + RDRAND + RDSEED |
| PRE-04 | Boot Integrity | CRC32 of stage1 (512B), stage2 (128KB), stage3 (32KB) |
| PRE-05 | Anti-Tamper | CRC32 comparison against expected, boot counter at 0x7FF0 |
| PRE-06 | DMA Protection | PCI bus scan (32 devices × 8 functions), BAR MMIO detection, IOMMU presence check |
| PRE-07 | SMI Counter | MSR 0x34 delta — detects System Management Interrupt activity |

## Kernel

| Feature | Detail |
|---------|--------|
| GDT + TSS | Kernel/user stack switching |
| IDT | 256 entries, ISR stubs in `isr.asm` |
| Syscalls | 25 handlers via MSR LSTAR (SYSCALL/SYSRET) |
| Scheduler | Round-robin, timer preemption (100Hz PIT, 3-pass TSC calibration: 50ms + 100ms + 200ms) |
| Context switch | Callee-saved registers, saved_rsp, `syscall_entry` in ASM with `sysretq` encoded as `db 0x48, 0x0F, 0x07` |
| Processes | Ring-0 and Ring-3, full fork with 4-level page table clone, sleep/wake via timer ticks |
| Address space | PML4 → PDPT → PD → PT clone/destroy, demand paging, kernel mappings (entries 256-511) shared |
| Snapshot | Disk-based kernel snapshot with CRC32 verification, tries snapshot before raw sector load |
| Kernel log | 256-entry ring buffer with timestamps + severity (EMERG→DEBUG), `dmesg` to dump |

### Memory (3-Tier Allocator)

```
┌─────────────────────────────────┐
│  Slab Allocator                 │  16/32/64/128/256-byte caches
├─────────────────────────────────┤
│  kmalloc Heap                   │  First-fit, splitting, coalescing
├─────────────────────────────────┤
│  PMM Bitmap Allocator           │  E820-aware, page-granularity
└─────────────────────────────────┘
```

- **fcache** — file cache with ATA write-back, dirty tracking, CRC32

### Filesystem

- **BrainFS** — FAT with 9 widths: 1, 2, 4, 8, 12, 16, 32, 64, and **128 bits per cluster** (each with its own EOC sentinel)
- **BrainVFS** — mount-point abstraction with pluggable backends
- **devfs, procfs, tmpfs**
- **VMM integration** — cluster I/O through virtual address space, fault-on-demand paging
- Config: `config/boot.cfg` (30+ fields, parsed at boot via `boot_args_parse()`)

## Drivers

| Driver | Notes |
|--------|-------|
| PS/2 keyboard | Extended scancodes (0xE0 prefix): arrows, delete, home/end/pgup/pgdn |
| PS/2 mouse | Scaled to 80x25 text coordinates (/4 X, /3 Y) |
| ATA PIO | LBA48, 4-drive auto-detect, model names, sector sizes |
| e1000 NIC | MMIO BAR0, EEPROM MAC read, 16-entry TX/RX rings, bus mastering, PCI class scan |
| xHCI USB 3.0 | Full MMIO: HCRST, command/event rings, per-slot state, control/bulk transfers, doorbell |
| USB mass storage | SCSI READ(10)/WRITE(10) via BBB protocol |

## Network

- Ethernet frame send/receive
- ARP cache (32 entries with aging)
- IPv4 packet dispatch with IP header checksum
- UDP sockets (16 max, port binding)
- ICMP echo reply + outgoing ping (with 500ms timeout, poll NIC)
- Static IP (192.168.1.100 default)
- Shell commands: `ifconfig`, `ping <ip>`, `usb`/`lsusb`

## Security (55 Modules)

All modules run during Stage 2 boot, before kernel load. Zero stub-only implementations.

| Category | Modules |
|----------|---------|
| **Firewalls** | Gen2 packet filter, stateful inspection, app-layer, adaptive anomaly detection |
| **DNS** | DNS-over-HTTPS, DNS-over-TLS, DNSCrypt (with X25519 key exchange) |
| **WiFi** | WPA2-AES (4-way handshake), WPA3-SAE (Dragonfly key exchange) |
| **MAC** | Random, clone, mask, ROT, OUI spoofing (40+ vendor prefixes) |
| **Anti-IP** | IPv4/IPv6 address randomization and rotation |
| **Disk UUID** | Random, serial mask, GPT header, MBR scramble |
| **Disk privacy** | UUID encrypt/clone/rot, label encrypt, serial mask, model spoof, LBA scramble, size obfuscation, FS hide, MBR/GPT mask |
| **Tor** | Core (3-hop AES-256-CTR + HMAC-SHA1 circuit encryption, padding, stream isolation), SOCKS5 proxy (RFC 1928), v3 hidden services (.onion address generation), directory client with bootstrap relay data |
| **NetGuard** | 4-thread SMP network scanner: VLAN-aware, telemetry hunter, 128-signature DB with wildcard mask matching, lock-free SPSC ring buffer (256 × 1514B slots, cache-line aligned), ASCII-art police chase animation with neon 256-color cycling |
| **Crypto** | AES-256 (ECB/CBC/CTR/GCM), SHA-256, SHA-512, ChaCha20, Poly1305, HKDF, PBKDF2-SHA1, Curve25519 |
| **John** | MD5, SHA-1, SHA-256, SHA-512, NTLM, DES, bcrypt, LM — 6 attack modes: wordlist, rules, brute force, rainbow table, hybrid, mask |
| **Pre-init** | 7-phase CPU/memory fingerprinting, RNG seed, DMA protection, boot integrity CRC32 |
| **Panic** | Full-screen register dump, reboot support |

### John the Reaper (2,539 lines across 7 files)

Built-in wordlist of 131 passwords. Rule mutations: prefix, suffix, replace, duplicate, toggle case. Rainbow tables with 4096-chain capacity. Configurable charset (lower/upper/digits/symbols/all). Max password 128 chars.

### NetGuard — ASCII Art Police Chase

```
    ______              ________
 __|______|__       __|______|__
|  ______  o|     |* _______  |
| |*-*  |   |     |  | @-@  | |
|_|______|__|     |__|______|_|
  (O)(O)(O)        (O)(O)(O)
   COP               SUSPECT
```

4-thread cooperative scheduler. Neon palette: 8 cycling 256-color values. Procedural city background with buildings and lit windows. HUD displays threats, scanned count, wiped count, telemetry killed.

## Shell

### Stage 2 — Fish Shell (3,453 lines)

- **95 recognized commands** including `grep`, `sort`, `uniq`, `wc`, `head`, `tail`, `tr`, `diff`, `base64`, `tee`, `xargs`, `nano`, `awk`, `hexdump`, `cal`, `uname`
- **17 built-in commands:** `cd`, `pushd`/`popd`/`dirs`, `pwd`, `set`, `export`, `echo`, `math`, `history`, `abbrev`, `string`, `count`, `type`, `realpath`, `source`
- **Recursive-descent math parser** — `+`, `-`, `*`, `/`, parenthesized sub-expressions, unary minus
- **Tab completion** — filesystem traversal via `vfs_readdir`, deduplication, common prefix insertion
- **Syntax highlighting** — character-by-character coloring on input line
- **10 Ctrl key bindings:** Ctrl+A (Home), Ctrl+E (End), Ctrl+K (Kill), Ctrl+U (Kill start), Ctrl+W (Delete word), Ctrl+R (Incremental search), Ctrl+D (EOF), Ctrl+C, Ctrl+L, Ctrl+Y (Yank)
- **Ctrl+Arrow keys** — word-level navigation
- **Incremental history search** — `(r-i-search)` prompt with live results
- **Wildcards** — `*` and `?` glob expansion via `vfs_readdir`
- **Pipe parsing** — splits on `|`, sequential segment execution
- **Abbreviations** — alias-like expansion
- **Boolean operators** — `and`, `or`, `not` based on `$status`
- **Security** — clears sensitive variables on exit

### Stage 2 — AWK (1,670 lines)

- **Custom regex engine** — compiled from scratch: `.`, `*`, `+`, `?`, `^`, `$`, `[...]`, `[^...]`, `(` `)`, `|`, `\` escapes
- **Full parser** — recursive-descent with `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `>=`, `<=`, `<`, `>`, `&&`, `||`
- **Built-in variables:** NR, NF, FNR, FS, OFS, RS, FILENAME
- **Pattern types:** regex `/pattern/`, expression, `BEGIN`, `END`
- **Field splitting** — single-char, multi-char (via regex), whitespace (default)
- **50 static functions**, zero libc dependency — implements own `strlen`, `strcmp`, `memcpy`, `atoi`, `itoa`

### Stage 2 — GUI (1,043 lines)

- Text-mode (80x25) with **box-drawing outlines** (0xDA/0xBF/0xC0/0xD9/0xC4/0xB3)
- **5 themes:** Classic, Ocean, Forest, Sunset, Midnight — 6 color attributes each
- **7 app types:** Terminal (2048B buffer), File Manager (32 entries, scroll), System Info, About, Settings, Process Monitor
- **Max 8 windows** with z-order tracking, drag-to-move, close button
- **Start menu** — 6 items with keyboard navigation
- **Desktop icons** — 5 icons with double-click-to-open
- **Taskbar** — active window highlighting, clock
- **Mouse** — cursor save/restore of underlying cell, click handling

### Kernel Shell

- `ps`, `mem`, `pci`, `date`, `ifconfig`, `usb`/`lsusb`, `ping`
- `dmesg` — kernel ring buffer with timestamps and severity-colored output
- `neofetch` — ASCII art Chicago skyline with CPU brand, memory, uptime, disk, NIC, PCI, USB
- `hyfetch` — same system info recolored with the HyFetch pride-flag (rainbow) gradient
- `btop` — 7-section system monitor: CPU (vendor/brand via CPUID), memory, disk (ATA models), network (e1000 status), PCI count, process table with state colors, uptime — live refresh
- Command history (Up/Down arrows), Ctrl+A/E/K/U/L shortcuts

## Layout

```
chicago95/
  stage1/boot.asm              # MBR (512B)
  stage2/
    main.c                     # Boot flow, 7-phase pre-init, 28 security module init
    boot/
      pre_init.c               # PRE-01 through PRE-07 (532 lines)
      config.c                 # boot.cfg parser (30+ fields)
    fs/
      brainfs_core.c           # BrainFS: 9 FAT widths, VMM integration (781 lines)
      brainfs_fat.c            # FAT read/write
      brainvfs.c               # Mount-point VFS layer
      encfs_mount.c            # Encrypted filesystem mount
    shell/
      fish_shell.c             # Fish shell (3,453 lines, 95 commands)
      awk.c                    # AWK with regex engine (1,670 lines)
      gnu_tools.c              # grep, sort, uniq, wc, head, tail, tr, diff...
    gui/
      gui.c                    # Text-mode windowing system (1,043 lines)
    memory/
      pmm.c                    # Physical memory bitmap
      vmm.c                    # Virtual memory, 4-level page tables
    security/                  # 55 modules
      firewall/                # Gen2 packet filter, stateful, app-layer, adaptive
      dns_encrypt/             # DoH, DoT, DNSCrypt
      wifi_encrypt/            # WPA2-AES, WPA3-SAE
      mac_encrypt/             # Random, clone, mask, ROT, OUI
      anti_ip/                 # IPv4/IPv6 randomization
      disk_encrypt/            # UUID, serial, GPT, MBR, LUKS, label, smart
      disk_privacy/            # UUID encrypt/clone/rot, serial mask, model spoof, LBA, size, FS hide
      tor/                     # Core, SOCKS5, hidden services, directory (1,412 lines)
      netguard/                # Network scanner + ASCII art chase (1,179 lines)
      common/                  # AES-256, SHA-256/512, ChaCha20, Poly1305, HKDF, RFC4
      john/                    # Password cracker (2,539 lines, 7 files)
  stage3/
    loader.c                   # ELF64 loader (higher-half aware, subtracts KERNEL_BASE)
  kernel/
    kernel.c                   # kernel_main: PCI scan → e1000/xHCI init → net_init → shell
    shell.c                    # Kernel shell: dmesg, neofetch, hyfetch, ps, mem, btop, command history
    kmsg.c                     # Kernel ring buffer: 256 entries, timestamps, severity levels
    btop.c                     # System monitor (453 lines, 7 sections)
    process.c                  # Process manager: create, fork, exit, sleep, scheduler (498 lines)
    syscall.c                  # 25 syscall handlers
    vfs.c                      # Tree VFS with devfs/procfs/tmpfs
    kmalloc.c                  # 3-tier allocator + fcache
    pci.c                      # PCI config, BAR[0-5] storage, class search
    ata.c                      # ATA PIO, 4-drive detect, LBA48
    timer.c                    # PIT 100Hz, scheduler hookup
    keyboard.c                 # PS/2 extended scancodes
    drivers/
      e1000.c                  # e1000 NIC (MMIO, TX/RX rings)
      xhci.c                   # xHCI USB 3.0 (command/event rings, doorbell)
      usb.c                    # USB mass storage (SCSI BBB)
      net.c                    # ARP, IPv4, UDP, ICMP, outgoing ping
    isr.asm                    # ISR stubs, syscall_entry (SYSCALL/SYSRET), context_switch
    include/                     # All headers (console, kmsg, process, pci, ata...)
  config/boot.cfg              # Boot configuration
```

## License

MIT License. See [LICENSE](LICENSE).
