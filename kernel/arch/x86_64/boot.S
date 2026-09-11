.intel_syntax noprefix

/* axiomeOS 64-bit axboot entry.
 *
 * Entered directly from the UEFI bootloader (BOOTX64.EFI) in long mode
 * with paging ON:
 *   RDI = physical address of struct axboot_info
 *   loader page tables live: identity 0-8G + kernel higher-half alias
 *   interrupts disabled, loader GDT live, loader stack live
 *
 * This stub parks the bootinfo pointer, switches to the bootstrap stack,
 * zeroes .bss, installs the kernel's own page tables (same geometry the
 * mmu.c code expects), reloads GDT/IDT and calls kmain(axboot_info).
 */

.set KERNEL_VIRT_BASE, 0xFFFFFFFF80000000

.section .bss, "aw", @nobits
.align 4096
.global pml4_table
pml4_table:
    .space 4096
.global pdp_table
pdp_table:
    .space 4096
.global pd_table
pd_table:
    .space 4096
.global pd_table2
pd_table2:
    .space 4096
.global pd_table3
pd_table3:
    .space 4096

.align 16
.global bootstrap_stack_top
bootstrap_stack:
    .space 16384
bootstrap_stack_top:

.section .data
.align 8
.global saved_bootinfo
saved_bootinfo:
    .quad 0

.section .text
.code64
.global _start
_start:
    cli
    cld

    /* Park the incoming bootinfo physical address first. saved_bootinfo
       lives in .data, so the .bss wipe below does not clobber it. */
    lea rax, [rip + saved_bootinfo]
    mov [rax], rdi

    /* Bootstrap stack (virtual; covered by the loader's high alias until
       our own tables are live, and by our tables afterwards). */
    lea rsp, [rip + bootstrap_stack_top]

    /* Zero .bss (tables + stack + idt) using virtual addresses. */
    lea rdi, [rip + _bss_start]
    lea rcx, [rip + _bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    lea rdi, [rip + pml4_table]
    lea rsi, [rip + pdp_table]
    lea rdx, [rip + pd_table]

    /* Page-table entries and CR3 need PHYSICAL addresses, but RIP-relative
       LEA yields virtual ones (high alias). KVIRT = phys + 64-bit base. */
    movabs rax, KERNEL_VIRT_BASE
    mov rbx, rsi
    sub rbx, rax                 # rbx = phys pdp_table
    mov rcx, rdx
    sub rcx, rax                 # rcx = phys pd_table
    mov r8, rdi
    sub r8, rax                  # r8 = phys pml4_table

    /* pml4[0] = pdp (identity), pml4[511] = pdp (higher-half alias),
       pml4[1] = pdp (mmu.c window root: pdp[2]=pd_table2, pdp[3]=pd_table3). */
    mov rax, rbx
    or rax, 0x03
    mov [rdi], rax
    mov [rdi + 511*8], rax
    mov [rdi + 1*8], rax

    /* pdp[0] = pd (identity), pdp[510] = pd (higher-half),
       pdp[2] = pd_table2 (0x80000000 window),
       pdp[3] = pd_table3 (0xC0000000 window). */
    mov rax, rcx
    or rax, 0x03
    mov [rsi], rax
    mov [rsi + 510*8], rax

    lea rax, [rip + pd_table2]
    movabs r9, KERNEL_VIRT_BASE
    sub rax, r9
    or rax, 0x03
    mov [rsi + 2*8], rax

    lea rax, [rip + pd_table3]
    sub rax, r9
    or rax, 0x03
    mov [rsi + 3*8], rax

    /* Identity-map 0-64MiB with 2MiB huge pages. This covers the kernel
       image, the axboot_info at 0x180000 and the early PMM bitmap area;
       mmu_arch_init() fills the rest and skips present entries. */
    xor ecx, ecx
1:
    mov eax, ecx
    shl rax, 21
    or rax, 0x83
    mov [rdx + rcx*8], rax
    inc rcx
    cmp rcx, 32
    jb 1b

    /* Install our tables (CR3 takes the physical address). */
    mov cr3, r8

    /* Enable SYSCALL/SYSRET (EFER.SCE, bit 0), mirroring the old 32-bit
       entry which set it during the mode switch. syscall_init() programs
       STAR/LSTAR/SFMASK but relies on SCE already being on. */
    mov ecx, 0xC0000080
    rdmsr
    bts eax, 0
    wrmsr

    /* Clear CET bits (like the old entry tail did). */
    mov rax, cr4
    btr rax, 20
    btr rax, 21
    btr rax, 22
    btr rax, 23
    mov cr4, rax

    /* Our GDT, then reload CS to our 64-bit code segment (0x18). */
    lea rax, [rip + gdt_ptr_64]
    lgdt [rax]

    push 0x18
    lea rax, [rip + 10f]
    push rax
    lretq
10:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    /* Null-handler IDT (real handlers installed later by isr_init). */
    lea rdi, [rip + idt_table]
    mov ecx, 256
2:
    lea rsi, [rip + null_handler]
    mov [rdi], si
    mov word ptr [rdi + 2], 0x0018
    mov byte ptr [rdi + 4], 0x00
    mov byte ptr [rdi + 5], 0x8E
    shr rsi, 16
    mov [rdi + 6], si
    shr rsi, 16
    mov [rdi + 8], esi
    mov dword ptr [rdi + 12], 0
    add rdi, 16
    dec ecx
    jnz 2b

    lea rax, [rip + idt_ptr]
    lidt [rax]

    /* kmain(axboot_info_phys). */
    mov rdi, [rip + saved_bootinfo]
    call kmain

    cli
3:
    hlt
    jmp 3b

null_handler:
    iretq

.section .data
.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AFFA000000FFFF
    .quad 0x00CFF2000000FFFF
gdt_end:
.global gdt_tss_desc
gdt_tss_desc:
    .quad 0x0000000000000000
    .quad 0x0000000000000000

gdt_ptr_64:
    .word gdt_tss_desc - gdt_start + 15
    .quad gdt_start

idt_ptr:
    .word 256 * 16 - 1
    .quad idt_table

.section .bss
.align 16
.global idt_table
idt_table:
    .space 256 * 16
