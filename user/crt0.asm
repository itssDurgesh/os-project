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

section .note.GNU-stack noalloc noexec nowrite progbits
