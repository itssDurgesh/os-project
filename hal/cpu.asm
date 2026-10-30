; Small CPU helpers that can't be written in C.

TSS_SELECTOR equ 0x28

section .text

; void gdt_load(const struct gdt_pointer *pointer)
global gdt_load
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, 0x10                    ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload_cs             ; a far jump is the only way to set CS

.reload_cs:
    mov ax, TSS_SELECTOR
    ltr ax
    ret

; void enter_user_mode(const struct regs *r)
; Starts the first user program by "returning" through a struct regs built by hand.
global enter_user_mode
extern isr_return

enter_user_mode:
    mov esp, [esp + 4]
    jmp isr_return

section .note.GNU-stack noalloc noexec nowrite progbits
