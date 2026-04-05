/*
 * crash_handler.c - Enhanced crash diagnostics for klawed
 */

/* Define _XOPEN_SOURCE before any includes for ucontext.h */
#define _XOPEN_SOURCE 700

#include "crash_handler.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

/* Platform-specific includes */
#ifdef __APPLE__
    #include <mach/mach.h>
    #include <mach/thread_act.h>
    #include <mach/task.h>
    #include <execinfo.h>
    #include <dlfcn.h>
#else
    #include <execinfo.h>
#endif

#include <ucontext.h>

/* Wrapper to suppress write return value warnings */
static inline void safe_write(int fd, const void *buf, size_t count) {
    ssize_t unused __attribute__((unused)) = write(fd, buf, count);
    (void)unused;
}

/* For REG_RIP, REG_RSP, REG_RBP on Linux */
#ifndef __APPLE__
    #include <sys/ucontext.h>
#endif

/* Signal info buffer */
static char g_crash_info[4096] = {0};
static volatile int g_crash_in_progress = 0;

/* emergency_cleanup_subagents is declared in klawed.c but called from here.
 * It's defined non-static so we can call it during crash handling. */
void emergency_cleanup_subagents(void);

/* Forward declarations */
static void crash_handler(int sig, siginfo_t *info, void *context);
static void log_backtrace(void);
static void log_registers(ucontext_t *uc);
static void log_thread_info(void);

/*
 * Signal names for human-readable output
 */
static const char *signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
        case SIGBUS:  return "SIGBUS (Bus Error)";
        case SIGILL:  return "SIGILL (Illegal Instruction)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGFPE:  return "SIGFPE (Floating Point Exception)";
        case SIGTRAP: return "SIGTRAP (Trap)";
        default:      return "Unknown Signal";
    }
}

/*
 * Get signal code description
 */
static const char *signal_code_desc(int sig, int code) {
    if (sig == SIGSEGV) {
        switch (code) {
            case SEGV_MAPERR:  return "Address not mapped to object";
            case SEGV_ACCERR:  return "Access permissions fault";
#ifdef SEGV_BNDERR
            case SEGV_BNDERR:  return "Bounds checking fault";
#endif
#ifdef SEGV_PKUERR
            case SEGV_PKUERR:  return "Protection key violation";
#endif
            default:           return "Unknown SEGV code";
        }
    } else if (sig == SIGBUS) {
        switch (code) {
            case BUS_ADRALN:   return "Invalid address alignment";
            case BUS_ADRERR:   return "Non-existent physical address";
            case BUS_OBJERR:   return "Object-specific hardware error";
            default:           return "Unknown BUS code";
        }
    } else if (sig == SIGFPE) {
        switch (code) {
            case FPE_INTDIV:   return "Integer divide by zero";
            case FPE_INTOVF:   return "Integer overflow";
            case FPE_FLTDIV:   return "Floating point divide by zero";
            case FPE_FLTOVF:   return "Floating point overflow";
            case FPE_FLTUND:   return "Floating point underflow";
            case FPE_FLTRES:   return "Floating point inexact result";
            case FPE_FLTINV:   return "Invalid floating point operation";
            default:           return "Unknown FPE code";
        }
    } else if (sig == SIGILL) {
        switch (code) {
            case ILL_ILLOPC:   return "Illegal opcode";
            case ILL_ILLOPN:   return "Illegal operand";
            case ILL_ILLADR:   return "Illegal addressing mode";
            case ILL_ILLTRP:   return "Illegal trap";
            case ILL_PRVOPC:   return "Privileged opcode";
            case ILL_PRVREG:   return "Privileged register";
            case ILL_COPROC:   return "Coprocessor error";
            case ILL_BADSTK:   return "Internal stack error";
            default:           return "Unknown ILL code";
        }
    }
    return "Unknown code";
}

/*
 * Install enhanced crash handlers
 */
void crash_handler_install(void) {
    struct sigaction sa = {0};

    sa.sa_sigaction = crash_handler;
    sa.sa_flags = (int)(SA_SIGINFO | SA_RESETHAND); /* Get siginfo, reset to default after */

    /* Block other signals during handler */
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGSEGV);
    sigaddset(&sa.sa_mask, SIGBUS);
    sigaddset(&sa.sa_mask, SIGILL);
    sigaddset(&sa.sa_mask, SIGABRT);
    sigaddset(&sa.sa_mask, SIGFPE);
    sigaddset(&sa.sa_mask, SIGTRAP);

    /* Install handlers for fatal signals */
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);

    LOG_INFO("[CrashHandler] Enhanced crash diagnostics installed");
}

/*
 * Get last crash info
 */
const char *crash_handler_get_last_info(void) {
    return g_crash_info;
}

/*
 * Main crash handler
 */
static void crash_handler(int sig, siginfo_t *info, void *context) {
    /* Prevent recursive crashes */
    if (__sync_lock_test_and_set(&g_crash_in_progress, 1)) {
        /* Another crash is being handled, just abort */
        _exit(1);
    }

    /* Get thread ID */
    pthread_t self = pthread_self();

    /* Build crash info string */
    int len = snprintf(g_crash_info, sizeof(g_crash_info),
        "\n"
        "========================================================================\n"
        "KLAWED CRASH REPORT\n"
        "========================================================================\n"
        "Signal:     %s\n"
        "PID:        %d\n"
        "Thread ID:  %p\n"
        "Fault Addr: %p\n"
        "Code:       %d (%s)\n",
        signal_name(sig),
        getpid(),
        (void *)self,
        info->si_addr,
        info->si_code,
        signal_code_desc(sig, info->si_code)
    );

    /* Write to stderr immediately (in case logging is broken) */
    safe_write(STDERR_FILENO, g_crash_info, (size_t)len);

    /* Log to klawed log file */
    LOG_ERROR("%s", g_crash_info);
    LOG_ERROR("[CrashHandler] Signal %d at address %p", sig, info->si_addr);

    /* Log registers if we have context */
    if (context) {
        ucontext_t *uc = (ucontext_t *)context;
        log_registers(uc);
    }

    /* Log backtrace */
    log_backtrace();

    /* Log thread info */
    log_thread_info();

    /* Final message */
    const char *footer =
        "========================================================================\n"
        "End of crash report - process will now terminate\n"
        "========================================================================\n";
    safe_write(STDERR_FILENO, footer, strlen(footer));
    LOG_ERROR("%s", footer);

    /* Sync logs */
    fflush(stderr);

    /* Call external emergency cleanup if registered */
    emergency_cleanup_subagents();

    /* Re-raise signal with default handler to get core dump */
    signal(sig, SIG_DFL);
    raise(sig);
}

/*
 * Log backtrace
 */
static void log_backtrace(void) {
    void *buffer[100];
    int nptrs;
    char **strings;

    nptrs = backtrace(buffer, 100);
    strings = backtrace_symbols(buffer, nptrs);

    if (strings == NULL) {
        LOG_ERROR("[CrashHandler] Failed to get backtrace symbols");
        return;
    }

    LOG_ERROR("[CrashHandler] Stack backtrace (%d frames):", nptrs);
    safe_write(STDERR_FILENO, "\nStack backtrace:\n", 18);

    for (int i = 0; i < nptrs; i++) {
        LOG_ERROR("  #%d: %s", i, strings[i]);

        /* Also write to stderr */
        char frame_info[512];
        int len = snprintf(frame_info, sizeof(frame_info), "  #%d: %s\n", i, strings[i]);
        safe_write(STDERR_FILENO, frame_info, (size_t)len);
    }

    free(strings);

    safe_write(STDERR_FILENO, "\n", 1);
}

/*
 * Log register state
 */
static void log_registers(ucontext_t *uc) {
    LOG_ERROR("[CrashHandler] Register state:");
    safe_write(STDERR_FILENO, "\nRegister state:\n", 16);

#ifdef __APPLE__
    #if defined(__arm64__)
        /* Apple Silicon (ARM64) registers */
        _STRUCT_ARM_THREAD_STATE64 *regs = &uc->uc_mcontext->__ss;

        LOG_ERROR("  PC (Program Counter): 0x%016llx", regs->__pc);
        LOG_ERROR("  LR (Link Register):   0x%016llx", regs->__lr);
        LOG_ERROR("  SP (Stack Pointer):   0x%016llx", regs->__sp);
        LOG_ERROR("  FP (Frame Pointer):   0x%016llx", regs->__fp);

        char reg_buf[256];
        int len = snprintf(reg_buf, sizeof(reg_buf),
            "  PC: 0x%016llx  LR: 0x%016llx\n"
            "  SP: 0x%016llx  FP: 0x%016llx\n",
            regs->__pc, regs->__lr, regs->__sp, regs->__fp);
        safe_write(STDERR_FILENO, reg_buf, (size_t)len);

        /* Log general purpose registers */
        for (int i = 0; i < 8; i++) {
            LOG_ERROR("  X%-2d: 0x%016llx  X%-2d: 0x%016llx  X%-2d: 0x%016llx  X%-2d: 0x%016llx",
                     i, regs->__x[i],
                     i+8, regs->__x[i+8],
                     i+16, regs->__x[i+16],
                     i+24, regs->__x[i+24]);
        }

    #elif defined(__x86_64__)
        /* Intel x86_64 registers on macOS */
        _STRUCT_X86_THREAD_STATE64 *regs = &uc->uc_mcontext->__ss;

        LOG_ERROR("  RIP: 0x%016llx  RSP: 0x%016llx  RBP: 0x%016llx",
                 regs->__rip, regs->__rsp, regs->__rbp);
        LOG_ERROR("  RAX: 0x%016llx  RBX: 0x%016llx  RCX: 0x%016llx",
                 regs->__rax, regs->__rbx, regs->__rcx);
        LOG_ERROR("  RDX: 0x%016llx  RSI: 0x%016llx  RDI: 0x%016llx",
                 regs->__rdx, regs->__rsi, regs->__rdi);
    #endif
#else
    /* Linux - simplified register info to avoid compatibility issues */
    LOG_ERROR("  Note: See kernel log for full register state (dmesg)");
    (void)uc; /* Unused on Linux for now */
#endif

    safe_write(STDERR_FILENO, "\n", 1);
}

/*
 * Log thread information
 */
static void log_thread_info(void) {
#ifdef __APPLE__
    /* Get Mach task and thread info */
    task_t task = mach_task_self();
    thread_act_array_t threads;
    mach_msg_type_number_t thread_count;

    kern_return_t kr = task_threads(task, &threads, &thread_count);
    if (kr == KERN_SUCCESS) {
        LOG_ERROR("[CrashHandler] Active threads: %u", thread_count);

        char thread_buf[256];
        int len = snprintf(thread_buf, sizeof(thread_buf),
            "\nActive threads: %u\n", thread_count);
        safe_write(STDERR_FILENO, thread_buf, (size_t)len);

        for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
            /* Get basic thread info */
            thread_basic_info_data_t info;
            mach_msg_type_number_t info_count = THREAD_BASIC_INFO_COUNT;

            kr = thread_info(threads[i], THREAD_BASIC_INFO,
                            (thread_info_t)&info, &info_count);
            if (kr == KERN_SUCCESS) {
                const char *state_str = "unknown";
                switch (info.run_state) {
                    case TH_STATE_RUNNING:   state_str = "running"; break;
                    case TH_STATE_STOPPED:   state_str = "stopped"; break;
                    case TH_STATE_WAITING:   state_str = "waiting"; break;
                    case TH_STATE_UNINTERRUPTIBLE: state_str = "uninterruptible"; break;
                    case TH_STATE_HALTED:    state_str = "halted"; break;
                    default:                 state_str = "unknown"; break;
                }

                LOG_ERROR("  Thread %u: state=%s, cpu_usage=%d%%",
                         i, state_str, info.cpu_usage);

                len = snprintf(thread_buf, sizeof(thread_buf),
                    "  Thread %u: state=%s, cpu_usage=%d%%\n",
                    i, state_str, info.cpu_usage);
                safe_write(STDERR_FILENO, thread_buf, (size_t)len);
            }

            mach_port_deallocate(task, threads[i]);
        }

        vm_deallocate(task, (vm_address_t)threads,
                     sizeof(thread_act_t) * thread_count);

        safe_write(STDERR_FILENO, "\n", 1);
    }
#else
    /* On Linux, thread info is harder to get from within the process */
    LOG_ERROR("[CrashHandler] Thread info: see /proc/%d/task/ for details", getpid());
#endif
}
