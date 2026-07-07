.intel_syntax noprefix
.section .text

.extern syscall_dispatch
.extern syscall_user_rsp
.extern current_kstack_top
.extern syscall_uregs
.extern syscall_set_frame

.global syscall_entry
syscall_entry:
    mov [syscall_user_rsp], rsp
    mov rsp, [current_kstack_top]

    push 0x33
    push [syscall_user_rsp]
    push r11
    push 0x2B
    push rcx

    push rax
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    /* Record the iret-frame address (per-thread) for signal delivery. */
    lea rdi, [rsp + 8*13]
    call syscall_set_frame

    mov [syscall_uregs + 0], rcx
    mov rax, [syscall_user_rsp]
    mov [syscall_uregs + 8], rax
    mov [syscall_uregs + 16], r11
    mov [syscall_uregs + 24], rbx
    mov [syscall_uregs + 32], rbp
    mov [syscall_uregs + 40], r12
    mov [syscall_uregs + 48], r13
    mov [syscall_uregs + 56], r14
    mov [syscall_uregs + 64], r15

    mov rdi, [rsp + 8*12]
    mov rsi, [rsp + 8*11]
    mov rdx, [rsp + 8*10]
    mov rcx, [rsp + 8*9]
    mov r8,  [rsp + 8*8]
    mov r9,  [rsp + 8*7]
    call syscall_dispatch

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    add rsp, 8
    iretq
