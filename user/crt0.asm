; Program entry point. The kernel leaves argc and argv on the stack.

section .text
global _start
extern main
extern exit

_start:
    mov eax, [esp]                  ; argc
    lea ebx, [esp + 4]              ; argv
    push ebx
    push eax
    call main
    push eax
    call exit

; A signal handler returns here, so the kernel can put the program back.
global signal_return_stub
signal_return_stub:
    mov eax, 9                      ; SYS_SIGRETURN
    int 0x80

section .note.GNU-stack noalloc noexec nowrite progbits
