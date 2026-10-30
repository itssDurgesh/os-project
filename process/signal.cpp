// Signals: a way to tell a process that something happened.
//
// Sending a signal only sets a bit in the target's PCB. The signal really happens
// the next time that process is about to go back to user mode. If it has a
// handler, we change its saved registers so it "returns" into the handler.

#include "console.h"
#include "errno.h"
#include "hal.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"

#define INIT_PID 1
#define EXIT_STATUS_SIGNAL_BASE 128
#define USER_EFLAGS 0x202       // interrupts on

// What we put on the user stack when calling a signal handler.
struct SignalFrame {
    uint32_t return_address;    // the handler's `ret` goes here
    uint32_t signal_number;     // the handler's argument
    Registers saved_registers;
};

bool signal_pending()
{
    return current_process->pending_signals != 0;
}

int send_signal(Process *target, int signal_number)
{
    uint32_t flags = turn_off_interrupts();
    uint32_t handler = target->signal_handlers[signal_number];
    bool ignored = handler == SIG_IGN;

    // SIGCHLD does nothing unless the program asked for it.
    if (signal_number == SIGCHLD && handler == SIG_DFL)
        ignored = true;

    // SIGKILL can never be ignored.
    if (signal_number == SIGKILL)
        ignored = false;

    if (!ignored && target->state != PROCESS_TERMINATED) {
        target->pending_signals |= 1u << signal_number;
        wake_process_for_signal(target);
    }

    restore_interrupts(flags);
    return 0;
}

// Ctrl+C goes to every user program. The shell, login and init ignore SIGINT,
// so only the program you are running stops.
void send_signal_to_all_programs(int signal_number)
{
    for (Process *process = process_list(); process != nullptr;
         process = process->list_next) {
        if (process->page_dir != kernel_page_dir)
            send_signal(process, signal_number);
    }
}

// Changes the saved registers so the program runs its handler next.
static void start_handler(Registers *r, int signal_number)
{
    SignalFrame frame;
    uint32_t new_stack = (r->user_esp - sizeof(SignalFrame)) & ~3u;

    if (!user_range_valid((void *)new_stack, sizeof(SignalFrame), true))
        process_exit(EXIT_STATUS_SIGNAL_BASE + signal_number);

    frame.return_address = current_process->signal_return_address;
    frame.signal_number = signal_number;
    frame.saved_registers = *r;
    memcpy((void *)new_stack, &frame, sizeof(frame));

    r->user_esp = new_stack;
    r->eip = current_process->signal_handlers[signal_number];
}

// Called just before a process goes back to user mode.
void deliver_signals(Registers *r)
{
    for (int signal_number = 1; signal_number < NUM_SIGNALS; signal_number++) {
        uint32_t bit = 1u << signal_number;
        uint32_t handler;

        if ((current_process->pending_signals & bit) == 0)
            continue;

        current_process->pending_signals &= ~bit;
        handler = current_process->signal_handlers[signal_number];

        if (signal_number == SIGKILL || handler == SIG_DFL)
            process_exit(EXIT_STATUS_SIGNAL_BASE + signal_number);

        if (handler != SIG_IGN)
            start_handler(r, signal_number);
        return;
    }
}

int sys_kill(Registers *r)
{
    int pid = (int)r->ebx;
    int signal_number = (int)r->ecx;
    Process *target = find_process(pid);

    if (signal_number < 1 || signal_number >= NUM_SIGNALS)
        return -EINVAL;
    if (target == nullptr || target->state == PROCESS_TERMINATED)
        return -ESRCH;

    // Killing PID 1 would leave orphans with nobody to collect them.
    if (pid == INIT_PID)
        return -EPERM;

    if (current_process->uid != 0 && current_process->uid != target->uid)
        return -EPERM;

    return send_signal(target, signal_number);
}

int sys_sigaction(Registers *r)
{
    int signal_number = (int)r->ebx;

    if (signal_number < 1 || signal_number >= NUM_SIGNALS || signal_number == SIGKILL)
        return -EINVAL;

    current_process->signal_handlers[signal_number] = r->ecx;
    current_process->signal_return_address = r->edx;
    return 0;
}

// The handler has finished, so put back the registers we saved in start_handler.
int sys_sigreturn(Registers *r)
{
    SignalFrame frame;
    uint32_t frame_address = r->user_esp - sizeof(uint32_t);

    if (!user_range_valid((void *)frame_address, sizeof(frame), false))
        process_exit(EXIT_STATUS_SIGNAL_BASE + SIGKILL);

    memcpy(&frame, (void *)frame_address, sizeof(frame));

    // These come from user memory, so a program could have changed them. Only
    // take the normal registers, or it could give itself kernel privileges.
    r->eip = frame.saved_registers.eip;
    r->user_esp = frame.saved_registers.user_esp;
    r->ebx = frame.saved_registers.ebx;
    r->ecx = frame.saved_registers.ecx;
    r->edx = frame.saved_registers.edx;
    r->esi = frame.saved_registers.esi;
    r->edi = frame.saved_registers.edi;
    r->ebp = frame.saved_registers.ebp;
    r->eflags = USER_EFLAGS;
    return (int)frame.saved_registers.eax;
}
