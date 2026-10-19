// Windows x64: setjmp/longjmp propios (ABI de Windows: rcx = primer argumento, xmm6-15 y rbx/rbp/rdi/rsi/r12-r15 son de la funcion llamada).
// Guardan/restauran solo registros y la pila; no hay desenrollado SEH, asi que se puede saltar entre las pilas (fibras) de los hilos del invitado.
#ifdef _WIN32
#include "platform.h"
__asm__(R"(
.text
.globl rt_setjmp
.def rt_setjmp; .scl 2; .type 32; .endef
rt_setjmp:
    movq %rbx, 0(%rcx)
    movq %rbp, 8(%rcx)
    movq %rdi, 16(%rcx)
    movq %rsi, 24(%rcx)
    leaq 8(%rsp), %rax
    movq %rax, 32(%rcx)
    movq %r12, 40(%rcx)
    movq %r13, 48(%rcx)
    movq %r14, 56(%rcx)
    movq %r15, 64(%rcx)
    movq (%rsp), %rax
    movq %rax, 72(%rcx)
    stmxcsr 80(%rcx)
    movups %xmm6, 96(%rcx)
    movups %xmm7, 112(%rcx)
    movups %xmm8, 128(%rcx)
    movups %xmm9, 144(%rcx)
    movups %xmm10, 160(%rcx)
    movups %xmm11, 176(%rcx)
    movups %xmm12, 192(%rcx)
    movups %xmm13, 208(%rcx)
    movups %xmm14, 224(%rcx)
    movups %xmm15, 240(%rcx)
    xorl %eax, %eax
    ret

.globl rt_longjmp
.def rt_longjmp; .scl 2; .type 32; .endef
rt_longjmp:
    movl %edx, %eax
    testl %eax, %eax
    jnz 1f
    movl $1, %eax
1:
    movq 0(%rcx), %rbx
    movq 8(%rcx), %rbp
    movq 16(%rcx), %rdi
    movq 24(%rcx), %rsi
    movq 40(%rcx), %r12
    movq 48(%rcx), %r13
    movq 56(%rcx), %r14
    movq 64(%rcx), %r15
    ldmxcsr 80(%rcx)
    movups 96(%rcx), %xmm6
    movups 112(%rcx), %xmm7
    movups 128(%rcx), %xmm8
    movups 144(%rcx), %xmm9
    movups 160(%rcx), %xmm10
    movups 176(%rcx), %xmm11
    movups 192(%rcx), %xmm12
    movups 208(%rcx), %xmm13
    movups 224(%rcx), %xmm14
    movups 240(%rcx), %xmm15
    movq 32(%rcx), %rsp
    jmp *72(%rcx)
)");
#endif
