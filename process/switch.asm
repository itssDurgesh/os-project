; void switch_context(uint32_t *saved_esp, uint32_t next_esp)
; Saves the registers C expects to survive a call, stores this stack pointer,
; then loads the next thread's stack and returns on it.

section .text
global switch_context

switch_context:
    push ebx
    push esi
    push edi
    push ebp

    mov eax, [esp + 20]             ; saved_esp
    mov edx, [esp + 24]             ; next_esp
    mov [eax], esp
    mov esp, edx

    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
