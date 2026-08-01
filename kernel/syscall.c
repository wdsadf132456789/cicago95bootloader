/**
 * Chicago-95 System Call Handler
 * Dispatch table, user↔kernel copy, argument validation
 */

#include "syscall.h"
#include "kernel.h"
#include "process.h"
#include "gdt.h"
#include "timer.h"
#include "console.h"
#include "kmalloc.h"
#include "keyboard.h"
#include "vfs.h"
#include "elf.h"
#include <stdint.h>

/* MSR constants */
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_SFMASK      0xC0000084
#define MSR_EFER        0xC0000080

/* User/kernel segment base for STAR */
#define STAR_USER_CS    0x0023000000000000ULL
#define STAR_KERNEL_CS  0x0010000000000000ULL

/* Forward declaration of asm entry stub */
extern void syscall_entry(void);

/* The current process's fd table (from process.h fd_table[16]) is an int array
 * of file descriptors into the VFS. We redirect these through vfs. */

/* ---------- helper: copy from user ---------- */
int syscall_validate_user_ptr(uint64_t addr, uint64_t len) {
    if (addr == 0) return 0;

    process_t *proc = process_get_current();
    if (!proc) return 0;

    /* Kernel addresses are always invalid targets for user copies */
    if (addr + len < addr) return 0;  /* overflow check */

    /* User space: below 0x0000800000000000 (canonical lower half user range) */
    if (addr >= 0x0000800000000000ULL) return 0;
    if (addr + len >= 0x0000800000000000ULL) return 0;

    /* Check that pages are actually mapped via the process address space */
    if (proc->cr3) {
        uint64_t end = addr + len;
        if (end <= addr) end = addr + 1; /* handle len==0 */
        for (uint64_t a = addr & ~0xFFFULL; a < end; a += 0x1000) {
            if (vmm_translate(proc->cr3, a) == 0) return 0;
        }
    }

    return 1;
}

int syscall_copy_from_user(void *kdst, const void *usrc, uint64_t len) {
    if (!syscall_validate_user_ptr((uint64_t)usrc, len)) return -1;
    memcpy(kdst, usrc, len);
    return 0;
}

int syscall_copy_to_user(void *udst, const void *ksrc, uint64_t len) {
    if (!syscall_validate_user_ptr((uint64_t)udst, len)) return -1;
    memcpy(udst, ksrc, len);
    return 0;
}

/* ---------- helper: get syscall name for debug ---------- */
static const char *syscall_names[] __attribute__((unused)) = {
    "READ", "WRITE", "OPEN", "CLOSE", "FORK", "EXEC", "EXIT", "WAIT",
    "GETPID", "GETPPID", "BRK", "MMAP", "MUNMAP", "SBRK", "SLEEP",
    "YIELD", "STAT", "DUP", "PIPE", "IOCTL", "GETCWD", "CHDIR",
    "TIME", "SHUTDOWN", "REBOOT"
};

/* ---------- individual syscall implementations ---------- */

static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    /* Validate user buffer */
    if (!syscall_validate_user_ptr(buf, len)) return -1;

    /* fd 0 = stdin: read from keyboard buffer */
    if (fd == 0) {
        char *kbuf = (char *)buf;
        int ch = keyboard_getchar();
        if (ch < 0) return 0;
        kbuf[0] = (char)ch;
        /* Echo to console */
        if (ch == '\n') {
            console_putc('\n', 0x07);
        } else if (ch == '\b') {
            console_putc('\b', 0x07);
            console_putc(' ', 0x07);
            console_putc('\b', 0x07);
        } else {
            console_putc((char)ch, 0x07);
        }
        return 1;
    }

    /* fd 1,2 = stdout/stderr, but reading doesn't make sense */
    if (fd == 1 || fd == 2) return -1;

    /* Regular VFS fd */
    if (fd >= VFS_MAX_FDS) return -1;

    /* Use process fd_table to get VFS node */
    int vfs_fd = proc->fd_table[fd];
    if (vfs_fd < 0) return -1;

    vfs_node_t *node = vfs_fd_get(vfs_fd);
    if (!node) return -1;

    uint64_t *offset = vfs_fd_offset(vfs_fd);
    if (!offset) return -1;

    ssize_t result = vfs_read(node, *offset, (void *)buf, len);
    if (result > 0) *offset += (uint64_t)result;
    return result;
}

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    if (!syscall_validate_user_ptr(buf, len)) return -1;

    const char *src = (const char *)buf;

    /* stdout/stderr -> VGA console */
    if (fd == 0) return -1;
    if (fd == 1 || fd == 2) {
        /* Write in chunks to console */
        size_t written = 0;
        while (written < len) {
            size_t chunk = len - written;
            if (chunk > 256) chunk = 256;
            /* Copy small chunk to kernel buffer for null-termination */
            char tmp[257];
            memcpy(tmp, src + written, chunk);
            tmp[chunk] = '\0';
            console_puts(tmp, 0x07);
            written += chunk;
        }
        return (int64_t)len;
    }

    /* Regular VFS fd */
    if (fd >= VFS_MAX_FDS) return -1;

    int vfs_fd = proc->fd_table[fd];
    if (vfs_fd < 0) return -1;

    vfs_node_t *node = vfs_fd_get(vfs_fd);
    if (!node) return -1;

    uint64_t *offset = vfs_fd_offset(vfs_fd);
    if (!offset) return -1;

    uint64_t off = *offset;
    if (node->flags & O_APPEND) off = node->length;

    ssize_t result = vfs_write(node, off, (const void *)buf, len);
    if (result > 0) *offset = off + (uint64_t)result;
    return result;
}

static int64_t sys_open(uint64_t path_ptr, uint64_t flags, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    /* Copy path from user */
    char path[VFS_PATH_LEN];
    memset(path, 0, sizeof(path));

    if (!syscall_validate_user_ptr(path_ptr, 1)) return -1;
    /* Copy one byte at a time until null or limit */
    for (int i = 0; i < VFS_PATH_LEN - 1; i++) {
        char c;
        if (syscall_copy_from_user(&c, (const void *)(path_ptr + i), 1) < 0)
            return -1;
        path[i] = c;
        if (c == '\0') break;
    }

    vfs_node_t *node = vfs_open(path, (uint32_t)flags);
    if (!node) return -1;

    /* Find a free fd slot in process fd_table */
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (proc->fd_table[i] == -1) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        vfs_close(node);
        return -1;
    }

    int vfs_fd = vfs_fd_alloc(node, (uint32_t)flags);
    if (vfs_fd < 0) {
        vfs_close(node);
        return -1;
    }

    proc->fd_table[slot] = vfs_fd;
    return slot;
}

static int64_t sys_close(uint64_t fd, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    if (fd >= 16) return -1;
    int vfs_fd = proc->fd_table[fd];
    if (vfs_fd < 0) return -1;

    vfs_fd_close(vfs_fd);
    proc->fd_table[fd] = -1;
    return 0;
}

static int64_t sys_fork(uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (int64_t)process_fork();
}

/* Frame of the currently executing syscall, so exec can redirect the
 * sysret return directly into the loaded image. */
static syscall_frame_t *current_frame = NULL;

static int64_t sys_exec(uint64_t path_ptr, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;
    if (proc->pid == 0) return -1;  /* init cannot exec */

    /* Copy path from user */
    char path[VFS_PATH_LEN];
    memset(path, 0, sizeof(path));

    if (!syscall_validate_user_ptr(path_ptr, 1)) return -1;
    for (int i = 0; i < VFS_PATH_LEN - 1; i++) {
        char c;
        if (syscall_copy_from_user(&c, (const void *)(path_ptr + i), 1) < 0)
            return -1;
        path[i] = c;
        if (c == '\0') break;
    }

    /* Open and read the ELF header */
    vfs_node_t *node = vfs_open(path, O_RDONLY);
    if (!node) return -1;
    uint32_t file_size = node->length;

    elf64_header_t hdr;
    if (vfs_read(node, 0, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) {
        vfs_close(node);
        return -1;
    }
    if (!elf_validate(&hdr)) {
        vfs_close(node);
        return -1;
    }

    /* Read the whole image into a kernel buffer */
    uint8_t *image = (uint8_t *)kmalloc(file_size);
    if (!image) {
        vfs_close(node);
        return -1;
    }
    memset(image, 0, file_size);
    if (vfs_read(node, 0, image, file_size) != (ssize_t)file_size) {
        kfree(image);
        vfs_close(node);
        return -1;
    }
    vfs_close(node);

    /* Map the PT_LOAD segments in the process address space, then copy the
     * bytes through the kernel identity map (we stay on the kernel cr3). */
    if ((uint64_t)hdr.e_phoff + (uint64_t)hdr.e_phnum * hdr.e_phentsize > file_size) {
        kfree(image);
        return -1;
    }
    elf64_phdr_t *phdr = (elf64_phdr_t *)(image + hdr.e_phoff);
    for (int i = 0; i < hdr.e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_offset + phdr[i].p_filesz > file_size) continue;

        uint64_t seg_end = phdr[i].p_vaddr + phdr[i].p_memsz;
        uint64_t page = phdr[i].p_vaddr & ~0xFFFULL;
        uint64_t page_end = (seg_end + 0xFFFULL) & ~0xFFFULL;
        for (; page < page_end; page += PAGE_SIZE) {
            uint64_t phys = vmm_translate(proc->cr3, page);
            if (!phys) {
                void *pg = pmm_alloc_page();
                if (!pg) { kfree(image); return -1; }
                memset(pg, 0, PAGE_SIZE);
                vmm_map_page(proc->cr3, page, (uint64_t)pg, PML4_RW | PML4_USER);
                phys = (uint64_t)pg;
            }

            uint64_t seg_off = page - phdr[i].p_vaddr;
            /* Copy the file-backed bytes for this page */
            if (seg_off < phdr[i].p_filesz) {
                uint64_t n = phdr[i].p_filesz - seg_off;
                if (n > PAGE_SIZE) n = PAGE_SIZE;
                memcpy((void *)phys, image + phdr[i].p_offset + seg_off, n);
            }
            /* Zero the rest of the page that belongs to the segment */
            uint64_t page_end_off = seg_off + PAGE_SIZE;
            if (page_end_off > phdr[i].p_memsz) page_end_off = phdr[i].p_memsz;
            if (page_end_off > phdr[i].p_filesz) {
                uint64_t zstart = seg_off > phdr[i].p_filesz ? seg_off : phdr[i].p_filesz;
                if (zstart < page_end_off)
                    memset((void *)phys + (zstart - seg_off), 0, page_end_off - zstart);
            }
        }
    }
    uint64_t entry = hdr.e_entry;
    kfree(image);

    /* Reload the process to run the new image in user mode */
    proc->ring = RING3;
    proc->entry_point = entry;
    proc->context.rip = entry;
    proc->context.rflags = 0x202;
    proc->context.cs = USER_CS;
    proc->context.ss = USER_DS;
    proc->context.rsp = proc->user_stack;

    /* Redirect the sysret return into the new image (never returns to caller) */
    if (current_frame) {
        current_frame->rip = entry;
        current_frame->rsp = proc->user_stack;
        current_frame->rflags = 0x202;
    }

    return 0;
}

static int64_t sys_exit(uint64_t code, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    /* Close all open files first */
    process_t *proc = process_get_current();
    if (proc) {
        for (int i = 0; i < 16; i++) {
            if (proc->fd_table[i] >= 0) {
                vfs_fd_close(proc->fd_table[i]);
                proc->fd_table[i] = -1;
            }
        }
    }
    process_exit((int)code);
    return 0; /* never reached */
}

static int64_t sys_wait(uint64_t status_ptr, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    /* Simple busy-wait: find a zombie child */
    int mypid = proc->pid;
    for (;;) {
        int found = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t *p = &processes[i];
            if (p->ppid == mypid && p->state == PROC_ZOMBIE) {
                /* Return the child's pid, write exit status */
                if (status_ptr && syscall_validate_user_ptr(status_ptr, sizeof(int))) {
                    int ec = p->exit_code;
                    memcpy((void *)status_ptr, &ec, sizeof(int));
                }
                int child_pid = p->pid;
                p->state = PROC_UNUSED;
                return child_pid;
            }
            if (p->ppid == mypid && p->state != PROC_UNUSED &&
                p->state != PROC_ZOMBIE) {
                found = 1;
            }
        }
        if (!found) return -1; /* no children */
        /* Yield and retry */
        process_yield();
    }
}

static int64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;
    return proc->pid;
}

static int64_t sys_getppid(uint64_t a1, uint64_t a2, uint64_t a3,
                            uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;
    return proc->ppid;
}

static int64_t sys_brk(uint64_t addr, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    if (addr == 0) return (int64_t)proc->brk;

    /* Don't let brk go below initial heap */
    uint64_t heap_start = proc->user_heap;
    if (addr < heap_start) return -1;

    /* Don't exceed reasonable limit (64MB) */
    if (addr - heap_start > 64 * 1024 * 1024) return -1;

    proc->brk = addr;
    if (addr > proc->user_heap_end) {
        /* Map new pages up to brk */
        uint64_t old_end = proc->user_heap_end;
        uint64_t new_end = (addr + 0xFFF) & ~0xFFFULL;
        for (uint64_t a = old_end; a < new_end; a += 0x1000) {
            void *page = pmm_alloc_page();
            if (!page) break;
            memset(page, 0, 0x1000);
            vmm_map_page(proc->cr3, a, (uint64_t)page, 0x07); /* present|rw|user */
        }
        proc->user_heap_end = new_end;
    }
    return (int64_t)proc->brk;
}

static int64_t sys_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;
    if (!proc->cr3) return -1;
    if (length == 0) return -1;

    /* Round up to page boundary */
    uint64_t pages = (length + 0xFFF) / 0x1000;
    uint64_t size = pages * 0x1000;

    /* If addr==0, pick an address above brk */
    if (addr == 0) {
        addr = (proc->brk + 0xFFFFF) & ~0xFFFFF; /* align to 1MB */
        /* Find a gap */
        addr = proc->user_heap_end + 0x100000; /* simple: just after heap */
    }

    /* Map pages */
    uint64_t pflags = 0x07; /* present | rw | user */
    if (prot & 0x1) pflags = 0x05; /* read-only: present | user */
    (void)pflags;
    for (uint64_t a = addr; a < addr + size; a += 0x1000) {
        void *page = pmm_alloc_page();
        if (!page) {
            /* Roll back */
            for (uint64_t b = addr; b < a; b += 0x1000) {
                uint64_t phys = vmm_translate(proc->cr3, b);
                if (phys) {
                    vmm_unmap_page(proc->cr3, b);
                    pmm_free_page((void *)(phys & ~0xFFFULL));
                }
            }
            return -1;
        }
        memset(page, 0, 0x1000);
        vmm_map_page(proc->cr3, a, (uint64_t)page, 0x07);
    }
    return (int64_t)addr;
}

static int64_t sys_munmap(uint64_t addr, uint64_t length, uint64_t a3,
                           uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc || !proc->cr3) return -1;

    uint64_t pages = (length + 0xFFF) / 0x1000;
    uint64_t size = pages * 0x1000;

    for (uint64_t a = addr; a < addr + size; a += 0x1000) {
        uint64_t phys = vmm_translate(proc->cr3, a);
        if (phys) {
            vmm_unmap_page(proc->cr3, a);
            pmm_free_page((void *)(phys & ~0xFFFULL));
        }
    }
    return 0;
}

static int64_t sys_sbrk(uint64_t increment, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    uint64_t old_brk = proc->brk;

    if (increment == 0) return (int64_t)old_brk;

    if (increment > 0) {
        /* Grow */
        uint64_t new_brk = old_brk + increment;
        /* Align up */
        new_brk = (new_brk + 0xFFF) & ~0xFFFULL;
        uint64_t old_page_end = (old_brk + 0xFFF) & ~0xFFFULL;

        for (uint64_t a = old_page_end; a < new_brk; a += 0x1000) {
            void *page = pmm_alloc_page();
            if (!page) return (int64_t)old_brk;
            memset(page, 0, 0x1000);
            vmm_map_page(proc->cr3, a, (uint64_t)page, 0x07);
        }
        proc->brk = new_brk;
        if (new_brk > proc->user_heap_end)
            proc->user_heap_end = new_brk;
    } else {
        /* Shrink (don't actually unmap, just move brk back) */
        uint64_t new_brk = old_brk - (uint64_t)(-((int64_t)increment));
        if (new_brk < proc->user_heap) new_brk = proc->user_heap;
        proc->brk = new_brk;
    }
    return (int64_t)old_brk;
}

static int64_t sys_sleep(uint64_t ms, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_sleep(ms);
    return 0;
}

static int64_t sys_yield(uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_yield();
    return 0;
}

static int64_t sys_stat(uint64_t path_ptr, uint64_t statbuf_ptr, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    char path[VFS_PATH_LEN];
    memset(path, 0, sizeof(path));

    if (!syscall_validate_user_ptr(path_ptr, 1)) return -1;
    for (int i = 0; i < VFS_PATH_LEN - 1; i++) {
        char c;
        if (syscall_copy_from_user(&c, (const void *)(path_ptr + i), 1) < 0)
            return -1;
        path[i] = c;
        if (c == '\0') break;
    }

    vfs_node_t *node = vfs_resolve(path);
    if (!node) return -1;

    if (!syscall_validate_user_ptr(statbuf_ptr, 64)) return -1;

    return vfs_stat(node, (void *)statbuf_ptr);
}

static int64_t sys_dup(uint64_t oldfd, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;
    if (oldfd >= 16) return -1;

    int old_vfs_fd = proc->fd_table[oldfd];
    if (old_vfs_fd < 0) return -1;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (proc->fd_table[i] == -1) { slot = i; break; }
    }
    if (slot < 0) return -1;

    int new_vfs_fd = vfs_fd_dup(old_vfs_fd);
    if (new_vfs_fd < 0) return -1;

    proc->fd_table[slot] = new_vfs_fd;
    return slot;
}

static int64_t sys_pipe(uint64_t pipefd_ptr, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;
    if (!syscall_validate_user_ptr(pipefd_ptr, sizeof(int) * 2)) return -1;

    /* Create a pipe: allocate a tmpfs node as the pipe buffer */
    vfs_node_t *pipe_read  = tmpfs_create_node("pipe_r", VFS_FILE);
    vfs_node_t *pipe_write = tmpfs_create_node("pipe_w", VFS_FILE);
    if (!pipe_read || !pipe_write) return -1;

    /* Link them: they share the same impl_data as a simple buffer */
    uint8_t *buf = kmalloc(4096);
    if (!buf) return -1;
    memset(buf, 0, 4096);
    pipe_read->impl_data  = (uint64_t)buf;
    pipe_write->impl_data = (uint64_t)buf;
    pipe_read->length  = 0;   /* bytes available */
    pipe_write->length = 4096; /* buffer capacity */

    int read_fd_slot = -1, write_fd_slot = -1;
    for (int i = 0; i < 16; i++) {
        if (proc->fd_table[i] == -1) {
            if (read_fd_slot < 0) read_fd_slot = i;
            else { write_fd_slot = i; break; }
        }
    }
    if (read_fd_slot < 0 || write_fd_slot < 0) return -1;

    int rfd = vfs_fd_alloc(pipe_read, O_RDONLY);
    int wfd = vfs_fd_alloc(pipe_write, O_WRONLY);
    if (rfd < 0 || wfd < 0) return -1;

    proc->fd_table[read_fd_slot]  = rfd;
    proc->fd_table[write_fd_slot] = wfd;

    int fds[2] = { read_fd_slot, write_fd_slot };
    memcpy((void *)pipefd_ptr, fds, sizeof(fds));
    return 0;
}

static int64_t sys_ioctl(uint64_t fd, uint64_t cmd, uint64_t arg,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    /* Stub: only handle terminal size query (TIOCGWINSZ = 0x5413) */
    if (fd == 0 || fd == 1 || fd == 2) {
        if (cmd == 0x5413 && syscall_validate_user_ptr(arg, 8)) {
            uint16_t winsize[4] = { 80, 25, 0, 0 };
            memcpy((void *)arg, winsize, 8);
            return 0;
        }
    }
    return -1;
}

static int64_t sys_getcwd(uint64_t buf_ptr, uint64_t size, uint64_t a3,
                           uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_validate_user_ptr(buf_ptr, size)) return -1;

    process_t *proc = process_get_current();
    if (!proc) return -1;

    const char *cwd = proc->cwd[0] ? proc->cwd : "/";
    size_t len = strlen(cwd);
    if (len >= size) return -1;

    memcpy((void *)buf_ptr, cwd, len + 1);
    return (int64_t)(len + 1);
}

static int64_t sys_chdir(uint64_t path_ptr, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *proc = process_get_current();
    if (!proc) return -1;

    /* Copy path from user */
    char path[VFS_PATH_LEN];
    memset(path, 0, sizeof(path));

    if (!syscall_validate_user_ptr(path_ptr, 1)) return -1;
    for (int i = 0; i < VFS_PATH_LEN - 1; i++) {
        char c;
        if (syscall_copy_from_user(&c, (const void *)(path_ptr + i), 1) < 0)
            return -1;
        path[i] = c;
        if (c == '\0') break;
    }

    /* Must resolve to an existing directory */
    vfs_node_t *dir = vfs_resolve(path);
    if (!dir || dir->type != VFS_DIR) return -1;

    /* Store per-process cwd */
    int i = 0;
    while (path[i] && i < VFS_PATH_LEN - 1) {
        proc->cwd[i] = path[i];
        i++;
    }
    proc->cwd[i] = '\0';
    return 0;
}

static int64_t sys_time(uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (int64_t)timer_get_ticks();
}

static int64_t sys_shutdown(uint64_t a1, uint64_t a2, uint64_t a3,
                             uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    console_puts("[SHUTDOWN] System halted.\n", 0x0C);
    cli();
    for (;;) hlt();
    return 0;
}

static int64_t sys_reboot(uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    console_puts("[REBOOT] Rebooting...\n", 0x0C);
    /* Triple-fault via bad IDT limit to force reset */
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr = { 0, 0 };
    asm volatile("lidt %0" : : "m"(idtr));
    asm volatile("int $3");
    return 0;
}

/* ---------- dispatch table ---------- */

static syscall_fn_t syscall_table[MAX_SYSCALLS] = {
    [SYS_READ]     = sys_read,
    [SYS_WRITE]    = sys_write,
    [SYS_OPEN]     = sys_open,
    [SYS_CLOSE]    = sys_close,
    [SYS_FORK]     = sys_fork,
    [SYS_EXEC]     = sys_exec,
    [SYS_EXIT]     = sys_exit,
    [SYS_WAIT]     = sys_wait,
    [SYS_GETPID]   = sys_getpid,
    [SYS_GETPPID]  = sys_getppid,
    [SYS_BRK]      = sys_brk,
    [SYS_MMAP]     = sys_mmap,
    [SYS_MUNMAP]   = sys_munmap,
    [SYS_SBRK]     = sys_sbrk,
    [SYS_SLEEP]    = sys_sleep,
    [SYS_YIELD]    = sys_yield,
    [SYS_STAT]     = sys_stat,
    [SYS_DUP]      = sys_dup,
    [SYS_PIPE]     = sys_pipe,
    [SYS_IOCTL]    = sys_ioctl,
    [SYS_GETCWD]   = sys_getcwd,
    [SYS_CHDIR]    = sys_chdir,
    [SYS_TIME]     = sys_time,
    [SYS_SHUTDOWN] = sys_shutdown,
    [SYS_REBOOT]   = sys_reboot,
};

/* ---------- main handler ---------- */

void syscall_handler(syscall_frame_t *frame) {
    current_frame = frame;
    uint64_t num = frame->rax;
    uint64_t a1  = frame->rdi;
    uint64_t a2  = frame->rsi;
    uint64_t a3  = frame->rdx;
    uint64_t a4  = frame->r10;
    uint64_t a5  = frame->r8;
    uint64_t a6  = frame->r9;

    if (num >= MAX_SYSCALLS || !syscall_table[num]) {
        frame->rax = (uint64_t)-1;
        return;
    }

    int64_t result = syscall_table[num](a1, a2, a3, a4, a5, a6);
    frame->rax = (uint64_t)result;
}

/* ---------- init: write MSR_LSTAR to point to syscall_entry ---------- */

/* Defined in assembly (syscall_entry.asm or isr.asm) */
extern void syscall_entry(void);

void syscall_init(void) {
    /* Set STAR: user CS = 0x23 (USER_CS), kernel CS = 0x08 (KERNEL_CS) */
    /* STAR layout: [sysret CS:SS] [syscall CS:SS] */
    uint64_t star = STAR_USER_CS | STAR_KERNEL_CS;
    asm volatile("wrmsr" : : "c"(MSR_STAR), "a"((uint32_t)star), "d"((uint32_t)(star >> 32)));

    /* Set LSTAR to syscall_entry */
    uint64_t entry = (uint64_t)syscall_entry;
    asm volatile("wrmsr" : : "c"(MSR_LSTAR), "a"((uint32_t)entry), "d"((uint32_t)(entry >> 32)));

    /* Mask interrupts on syscall entry */
    uint64_t sfmask = 0x200; /* mask IF */
    asm volatile("wrmsr" : : "c"(MSR_SFMASK), "a"((uint32_t)sfmask), "d"((uint32_t)(sfmask >> 32)));

    /* Enable SCE (syscall extensions) in EFER */
    uint32_t efer_lo, efer_hi;
    asm volatile("rdmsr" : "=a"(efer_lo), "=d"(efer_hi) : "c"(MSR_EFER));
    efer_lo |= 1; /* SCE bit */
    asm volatile("wrmsr" : : "c"(MSR_EFER), "a"(efer_lo), "d"(efer_hi));

    /* Initialize all fd tables */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        for (int j = 0; j < 16; j++) {
            processes[i].fd_table[j] = -1;
        }
    }

    console_puts("[SYSCALL] MSR_LSTAR initialized, syscall interface ready\n", 0x0A);
}
