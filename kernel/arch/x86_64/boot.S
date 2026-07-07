.intel_syntax noprefix

.set KERNEL_VIRT_BASE, 0xFFFFFFFF80000000
.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH, 0

.section .multiboot, "a"
.align 8
mb2_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long mb2_header_end - mb2_header_start
    .long -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))
    .word 0
    .word 0
    .long 8
mb2_header_end:

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
saved_magic:
    .quad 0
saved_mb2_info:
    .quad 0

.section .text
.code32
.global _start
_start:
    cld
    cli

    lea ecx, [saved_magic - KERNEL_VIRT_BASE]
    mov [ecx], eax
    lea ecx, [saved_mb2_info - KERNEL_VIRT_BASE]
    mov [ecx], ebx

    lea esp, [bootstrap_stack_top - KERNEL_VIRT_BASE]

    lea edi, [_bss_start - KERNEL_VIRT_BASE]
    lea ecx, [_bss_end - KERNEL_VIRT_BASE]
    sub ecx, edi
    xor eax, eax
    rep stosb

    lea edi, [pml4_table - KERNEL_VIRT_BASE]
    lea esi, [pdp_table - KERNEL_VIRT_BASE]
    lea edx, [pd_table - KERNEL_VIRT_BASE]

    mov eax, esi
    or eax, 0x03
    mov [edi], eax
    mov [edi + 511*8], eax

    mov eax, edx
    or eax, 0x03
    mov [esi], eax
    mov [esi + 510*8], eax

    mov eax, esi
    or eax, 0x03
    mov [edi + 1*8], eax

    lea eax, [pd_table2 - KERNEL_VIRT_BASE]
    or eax, 0x03
    mov [esi + 2*8], eax

    lea eax, [pd_table3 - KERNEL_VIRT_BASE]
    or eax, 0x03
    mov [esi + 3*8], eax

    mov dword ptr [edx], 0x000083
    mov dword ptr [edx + 4], 0x000000
    mov dword ptr [edx + 8], 0x200083
    mov dword ptr [edx + 12], 0x000000
    mov dword ptr [edx + 16], 0x400083
    mov dword ptr [edx + 20], 0x000000
    mov dword ptr [edx + 24], 0x600083
    mov dword ptr [edx + 28], 0x000000

    mov eax, edi
    mov cr3, eax

    mov eax, cr4
    bts eax, 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    bts eax, 8
    bts eax, 0
    wrmsr

    lea eax, [gdt_ptr_32 - KERNEL_VIRT_BASE]
    lgdt [eax]

    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    .byte 0xEA
    .long 1f - KERNEL_VIRT_BASE
    .word 0x18

.code64
1:
    lea rax, [rip + gdt_ptr_64]
    lgdt [rax]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

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

    lea rax, [rip + saved_magic]
    mov rdi, [rax]
    lea rax, [rip + saved_mb2_info]
    mov rsi, [rax]

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

gdt_ptr_32:
    .word gdt_tss_desc - gdt_start + 15
    .long gdt_start - KERNEL_VIRT_BASE

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
