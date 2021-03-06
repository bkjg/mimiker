#include <aarch64/asm.h>
#include <sys/ucontext.h>
#include <sys/syscall.h>

#include "aarch64/assym.h"

/* int setjmp(jmp_buf env) */
ENTRY(setjmp)
        sub     sp, sp, CALLFRAME_SIZ
        stp     lr, x19, [sp]
        /* Save env in safe register. */
        mov     x19, x0

        /* 
         * Save current signalmask at ucontext::uc_sigmask.
         * int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
         * 
         * If set is NULL, then the signal mask is unchanged (i.e., how is
         * ignored), but the current value of the signal mask is nevertheless
         * returned in oldset (if it is not NULL).
         */
        add     x2, x19, UC_MASK /* &env->uc_sigmask */
        mov     x1, xzr
        bl      sigprocmask
        cmp     x0, xzr
        bne     botch

        /* 
         * Save stack_t at ucontext::uc_stack
         * int sigaltstack(const stack_t *ss, stack_t *old_ss);
         *
         * By specifying ss as NULL, and old_ss as a non-NULL value, one can
         * obtain the current settings for the alternate signal stack without
         * changing them.
         */
        add     x1, x19, UC_STACK /* &env->uc_stack */
        /* We know that x0 is equal to 0 here. */
        bl      sigaltstack
        cmp     x0, xzr
        bne     botch

        /* stack_t::ss_flags is a int */
        ldr     w0, [x19, UC_STACK+SS_FLAGS]
        and     w0, w0, SS_ONSTACK
        cmp     w0, wzr
        beq     1f

        /* ucontext_t::uc_flags is a int */
        ldr     w0, [x19, UC_FLAGS]
        orr     w0, w0, _UC_STACK
        str     w0, [x19, UC_FLAGS]

1:
        /* restore jpmbuf */
        mov     x0, x19
        ldp     lr, x19, [sp]

        add     sp, sp, CALLFRAME_SIZ

        b       _setjmp
botch:
        bl      abort
END(setjmp)
