/* SysV x86-64 cooperative context switch. All ABI-preserved GPRs and FP control
 * state are saved. Caller-saved SIMD registers follow the ordinary C ABI. */
.text
.globl sp_context_swap
.type sp_context_swap,@function
sp_context_swap:
    movq %rsp, 0(%rdi)
    movq %rbx, 8(%rdi)
    movq %rbp, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %r13, 32(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 48(%rdi)
    stmxcsr 56(%rdi)
    fnstcw 60(%rdi)
    movq 0(%rsi), %rsp
    movq 8(%rsi), %rbx
    movq 16(%rsi), %rbp
    movq 24(%rsi), %r12
    movq 32(%rsi), %r13
    movq 40(%rsi), %r14
    movq 48(%rsi), %r15
    ldmxcsr 56(%rsi)
    fldcw 60(%rsi)
    ret
.size sp_context_swap,.-sp_context_swap
.section .note.GNU-stack,"",@progbits
