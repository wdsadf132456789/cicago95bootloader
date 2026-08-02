# Chicago-95 Bootloader Debugging and Kernel Clobber Fix

## Issue Summary
The Chicago-95 bootloader was hanging early during the `pre_init` phase. Investigation revealed a structural flaw: the kernel (loaded by stage1 at `0x10000..0x22800`) was clobbering a critical region of stage2's runtime code (`sec_memzero` and other security crypto functions), causing the system to execute kernel garbage upon the first call to `sec_memzero`, leading to a triple fault.

## Findings
- **Kernel/Stage2 Overlap:** The kernel load address `0x10000` overlapped with stage2's code at `0x600..0x80400`.
- **Hang Mechanism:** `pre_init` called `sec_memzero` at guest address `0x1d8d0` (which is inside the clobbered `[0x10000, 0x22800)` window). The guest attempted to execute kernel data as code, leading to an immediate crash.
- **SMI/Watchdog Stalls:** On QEMU q35 machine, periodic SMIs (likely from the ICH9 TCO watchdog) were stalling the guest in SMM, leading to further boot instability. Switching to `i440fx` (`-machine pc`) provided a more stable environment for debugging.

## Fix Implemented
1.  **Kernel Relocation:** Moved the kernel's physical load address from `0x10000` to `0x200000` (2MB), placing it safely above stage2 and stage3.
    - Modified `kernel/linker.ld` to set the kernel physical base to `0x200000`.
    - Modified `stage1/boot.asm` to load the kernel at `0x200000` (`KERNEL_SEG = 0x20000`, `KERNEL_OFF = 0`).
2.  **Debug Cleanup:** Removed temporary debug markers (`[P0]..[P4]`, `dbg_state`) from `stage2/main.c` and `stage2/boot/pre_init.c`.

## Verification
- The boot process now successfully completes `pre_init` and proceeds to stage3 and kernel handoff.
- The `[P0]` to `[P4]` progression in the serial log confirms all pre-initialization stages complete without corruption or crashing.
- System stability confirmed with `i440fx` machine.
