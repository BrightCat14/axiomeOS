.intel_syntax noprefix
.section .text

.extern tss
.extern current_kstack_top

.global context_switch
context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp

    mov rsp, rsi

    mov [current_kstack_top], rdx
    mov [tss + 4], rdx

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    sti
    ret

.global user_iret_stub
user_iret_stub:
    pop rax
    iretq
