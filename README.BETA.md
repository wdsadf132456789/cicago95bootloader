Chicago-95 / BrainFS Bootloader
================================

Status: BETA - Not for production use

This software is currently in beta. Expect bugs, incomplete features,
and breaking changes between versions. Do not use this in any
production or mission-critical environment.

Current version: 0.1.0-beta
Lines of code: ~1.4 million (bootloader + kernel)
Target: x86_64 bare-metal, ring-0, no OS dependency
Bootloader: 470KB | Kernel: 70KB

What works:
   Boot chain:
     - Stage 1 MBR (512 bytes, relocates to 0x0600)
     - Stage 2 (Real -> Protected -> Long mode, 55 security modules)
     - Stage 3 ELF64 loader (higher-half aware, p_vaddr - KERNEL_BASE)
     - Kernel entry at 0xFFFFFFFF80000000
     - Stages 5-100: boot manager menu with 58 unique demo templates
       (92 boot-time stages: VGA text-mode visual demos for Awk, PHP,
       JavaScript, Ruby, Python, Rust, Go, Lisp, SQL, Haskell, Brainfuck,
       Lua, C++, Bash, Perl, TypeScript, Kotlin, Swift, Dart, C#, Forth,
       Prolog, COBOL, Fortran, Julia, Zig, APL, Erlang, Elixir, Clojure,
       VHDL, R, Ada, Logo, Smalltalk, Assembly, Arch Linux + 22 classic)

   Pre-init hardening (7 phases, runs before everything):
    - PRE-01: CPU Fingerprint (CPUID leaves 0,1,2,7,0x80000002-6, XCR0/AVX)
    - PRE-02: Memory Fingerprint (E820 parse, overlap detection, CRC32 of table)
    - PRE-03: RNG Seed (256-bit: TSC jitter + I/O ports + RDRAND + RDSEED)
    - PRE-04: Boot Integrity (CRC32 of stage1/stage2/stage3)
    - PRE-05: Anti-Tamper (CRC32 comparison, boot counter at 0x7FF0)
    - PRE-06: DMA Protection (PCI bus scan, BAR MMIO detection, IOMMU check)
    - PRE-07: SMI Counter (MSR 0x34 delta to detect System Management Interrupts)

  Kernel core:
    - GDT + TSS (kernel/user stack switching)
    - IDT (256 entries, ISR stubs in isr.asm)
    - IRQ registration and dispatch
    - PIT timer at 100Hz (3-pass TSC calibration: 50ms + 100ms + 200ms)
    - Serial COM1 debug output
    - 25 syscall interface via MSR LSTAR (SYSCALL/SYSRET)
    - Round-robin process scheduler with timer preemption
    - Context switch (saved_rsp, callee-saved registers, sysretq in ASM)
    - Process sleep/wake (timer-based blocking)
    - Ring-0 and Ring-3 process creation
    - Full fork (4-level page table clone, PML4 -> PDPT -> PD -> PT)
    - Kernel snapshot boot (disk-based with CRC32 verification)
    - Kernel ring buffer (256-entry, timestamps, severity levels EMERG-DEBUG)

  Memory management (3-tier allocator):
    - PMM bitmap allocator (E820-aware, page-granularity)
    - kmalloc heap (first-fit, splitting, coalescing)
    - Slab allocator (16/32/64/128/256 byte object caches)
    - VMM (higher-half, 4-level page tables, on-demand paging)
    - fcache (file cache with ATA write-back, dirty tracking)

  Filesystem:
    - BrainFS: FAT with 9 widths (1, 2, 4, 8, 12, 16, 32, 64, 128 bits per cluster)
    - Each width has its own end-of-chain sentinel value
    - BrainVFS (mount-point abstraction with pluggable backends)
    - devfs, procfs, tmpfs
    - VMM integration (cluster I/O via virtual address space, fault-on-demand)
    - Config file parsing (config/boot.cfg, 30+ fields)

  Drivers:
    - PS/2 keyboard (extended scancodes via 0xE0 prefix: arrows, delete, home/end/pgup/pgdn)
    - PS/2 mouse (80x25 text coordinates, scaled via /4 and /3)
    - ATA PIO with LBA48 (4-drive detection, model names, sector sizes)
    - e1000 NIC (MMIO BAR0, EEPROM MAC, 16-entry TX/RX rings, bus mastering, PCI class scan)
    - xHCI USB 3.0 (HCRST, command/event rings, per-slot state, control/bulk transfers, doorbell)
    - USB mass storage (SCSI READ(10)/WRITE(10) via BBB protocol)

  Network:
    - Ethernet frame send/receive
    - ARP cache (32 entries, aging)
    - IPv4 packet dispatch (UDP + ICMP, IP header checksum)
    - UDP sockets (16 max, port binding)
    - ICMP echo reply + outgoing ping (500ms timeout, NIC poll)
    - Static IP (192.168.1.100 default)
    - Shell: ifconfig, ping <ip>, usb/lsusb

  Security (55 modules, all fully implemented):
    - Firewalls: Gen2 packet filter, stateful inspection, app-layer, adaptive anomaly detection
    - DNS encryption: DoH, DoT, DNSCrypt (X25519 key exchange)
    - WiFi: WPA2-AES (4-way handshake), WPA3-SAE (Dragonfly key exchange)
    - MAC: Random, clone, mask, ROT, OUI spoofing (40+ vendor prefixes)
    - Anti-IP: IPv4/IPv6 address randomization and rotation
    - Disk privacy: UUID encrypt/clone/rot, label encrypt, serial mask, model spoof,
        LBA scramble, size obfuscation, FS hide, MBR/GPT mask
    - Tor: Core (3-hop AES-256-CTR + HMAC-SHA1, padding, stream isolation),
        SOCKS5 proxy (RFC 1928), v3 hidden services (.onion generation),
        directory client with 6 bootstrap relays
    - NetGuard: 4-thread SMP scanner (VLAN-aware, telemetry hunter, 128-signature DB,
        lock-free SPSC ring buffer, ASCII-art police chase with neon 256-color animation)
    - Crypto: AES-256 (ECB/CBC/CTR/GCM), SHA-256/512, ChaCha20, Poly1305, HKDF, PBKDF2, Curve25519
    - John the Reaper: MD5/SHA1/SHA256/SHA512/NTLM/DES/bcrypt/LM,
        6 attack modes (wordlist, rules, brute force, rainbow, hybrid, mask),
        131-word built-in dictionary, 4096-chain rainbow tables
    - Pre-init: 7-phase CPU/memory fingerprinting, RNG seed, DMA protection, boot integrity
    - Panic: Full-screen register dump, reboot support
    - Security audit: all 55 modules audited, buffer overflows fixed (tor_hs, tor_core),
        VLAN handling patched (netguard), SOCKS5 output wired, TCP offset corrected

  Shell (Stage 2) — Fish Shell (3,453 lines):
    - 95 recognized commands (grep, sort, uniq, wc, head, tail, tr, diff, base64, tee, xargs...)
    - 17 built-in commands (cd, pushd/popd, pwd, set, export, echo, math, history, abbrev...)
    - Recursive-descent math parser (+, -, *, /, parentheses, unary minus)
    - Tab completion with filesystem traversal and common prefix insertion
    - Syntax highlighting (character-by-character)
    - 10 Ctrl key bindings (A/E/K/U/W/R/D/C/L/Y), Ctrl+Arrow word navigation
    - Incremental history search (r-i-search prompt)
    - Wildcard glob expansion (*, ?) via vfs_readdir
    - Pipe parsing (|), boolean operators (and/or/not)
    - Abbreviations, variables with export flag
    - Security: clears sensitive data on exit

  Shell (Stage 2) — AWK (1,670 lines):
    - Custom regex engine (., *, +, ?, ^, $, [...], [...], (), |, \)
    - Full recursive-descent parser (+, -, *, /, %, ==, !=, >=, <=, <, >, &&, ||)
    - Built-in variables: NR, NF, FNR, FS, OFS, RS, FILENAME
    - Pattern types: regex, expression, BEGIN, END
    - Field splitting (single-char, multi-char, whitespace default)
    - 50 static functions, zero libc dependency

  GUI (Stage 2) — Text-Mode Windowing System (1,043 lines):
    - Box-drawing outlines (single and double line styles)
    - 5 themes (Classic, Ocean, Forest, Sunset, Midnight), 6 color attributes each
    - 7 app types: Terminal (2048B buffer), File Manager (32 entries, scroll), System Info,
        About, Settings (theme/border toggle), Process Monitor
    - Max 8 windows with z-order tracking, drag-to-move, close button
    - Start menu (6 items), Desktop icons (5, double-click-to-open), Taskbar with clock
    - Mouse support: cursor save/restore, click handling for windows and taskbar

  Kernel shell:
    - ps, mem, pci, date, ifconfig, usb/lsusb, ping
    - dmesg (kernel ring buffer, timestamps, severity-colored output)
    - neofetch (ASCII art Chicago skyline with CPU brand, memory, uptime, disk, NIC, PCI, USB)
    - btop: 7-section dashboard (CPU vendor/brand, memory, disk, network, PCI, processes, uptime)
        with color-coded bars and live refresh
    - Command history (Up/Down arrows), Ctrl+A/E/K/U/L shortcuts

What does not work yet:
  - NVMe driver beyond stub
  - DHCP (static IP only)
  - TCP/IP stack
  - Kernel shell lacks pipe/redirection

Use at your own risk.
