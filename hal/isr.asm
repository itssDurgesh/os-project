; One small entry stub per interrupt vector. Each stub pushes the vector number
; and jumps to isr_common, which saves the CPU state and calls isr_dispatch().
; The CPU pushes an error code for some exceptions, so the other stubs push a
; dummy 0 to keep struct regs the same shape for every interrupt.

section .text
extern isr_dispatch

%macro ISR_STUB 1
isr_stub_%1:
    %if %1 == 8 || %1 == 10 || %1 == 11 || %1 == 12 || %1 == 13 || %1 == 14 || %1 == 17
    %else
        push dword 0
    %endif
    push dword %1
    jmp isr_common
%endmacro

%assign vector 0
%rep 256
    ISR_STUB vector
    %assign vector vector + 1
%endrep

isr_common:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10                    ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld
    push esp                        ; argument: struct regs *
    call isr_dispatch
    add esp, 4

; New user processes also start here, with a struct regs built by hand.
global isr_return
isr_return:
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8                      ; drop the vector and error code
    iret

section .data
global isr_table

%macro ISR_ENTRY 1
    dd isr_stub_%1
%endmacro

isr_table:
%assign vector 0
%rep 256
    ISR_ENTRY vector
    %assign vector vector + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
