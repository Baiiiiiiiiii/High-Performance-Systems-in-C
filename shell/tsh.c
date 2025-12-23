/**
 * @file tsh.c
 * @brief A tiny shell program with job control
 *
 * TODO: Delete this comment and replace it with your own.
 * <The line above is not a sufficient documentation.
 *  You will need to write your program documentation.
 *  Follow the 15-213/18-213/15-513/18-613 style guide at
 *  http://www.cs.cmu.edu/~18213/codeStyle.html.>
 *
 * @author Your Name <andrewid@andrew.cmu.edu>
 * TODO: Include your name and Andrew ID here.
 */

#include "csapp.h"
#include "tsh_helper.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * If DEBUG is defined, enable contracts and printing on dbg_printf.
 */
#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Function prototypes */
void eval(const char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);
void cleanup(void);

/**
 * @brief Entry point for the tiny shell (tsh).
 *
 * This function implements the shell's initialization and read-eval loop.
 * It processes command-line options and keeps reading command lines and execute
 * it from stdin.
 *
 * Command-line options:
 *   -h : print help
 *   -v : enable verbose
 *   -p : disables prompt printing
 *
 * Arguments: argc, argv
 *
 * Return value:
 *   Returns 0 on normal termination
 *   exit(1) on errors
 *
 */
int main(int argc, char **argv) {
    int c;
    char cmdline[MAXLINE_TSH]; // Cmdline for fgets
    bool emit_prompt = true;   // Emit prompt (default)

    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        perror("dup2 error");
        exit(1);
    }

    // Parse the command line
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h': // Prints help message
            usage();
            break;
        case 'v': // Emits additional diagnostic info
            verbose = true;
            break;
        case 'p': // Disables prompt printing
            emit_prompt = false;
            break;
        default:
            usage();
        }
    }

    // Create environment variable
    if (putenv(strdup("MY_ENV=42")) < 0) {
        perror("putenv error");
        exit(1);
    }

    // Set buffering mode of stdout to line buffering.
    // This prevents lines from being printed in the wrong order.
    if (setvbuf(stdout, NULL, _IOLBF, 0) < 0) {
        perror("setvbuf error");
        exit(1);
    }

    // Initialize the job list
    init_job_list();

    // Register a function to clean up the job list on program termination.
    // The function may not run in the case of abnormal termination (e.g. when
    // using exit or terminating due to a signal handler), so in those cases,
    // we trust that the OS will clean up any remaining resources.
    if (atexit(cleanup) < 0) {
        perror("atexit error");
        exit(1);
    }

    // Install the signal handlers
    Signal(SIGINT, sigint_handler);   // Handles Ctrl-C
    Signal(SIGTSTP, sigtstp_handler); // Handles Ctrl-Z
    Signal(SIGCHLD, sigchld_handler); // Handles terminated or stopped child

    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    Signal(SIGQUIT, sigquit_handler);

    // Execute the shell's read/eval loop
    while (true) {
        if (emit_prompt) {
            printf("%s", prompt);

            // We must flush stdout since we are not printing a full line.
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE_TSH, stdin) == NULL) && ferror(stdin)) {
            perror("fgets error");
            exit(1);
        }

        if (feof(stdin)) {
            // End of file (Ctrl-D)
            printf("\n");
            return 0;
        }

        // Remove any trailing newline
        char *newline = strchr(cmdline, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        // Evaluate the command line
        eval(cmdline);
    }

    return -1; // control never reaches here
}

/**
 * @brief Parse and execute a command line in the shell.
 *
 * Parses the command line arguments and executes
 * 1) a builtin command (quit, jobs, bg, fg)
 * or
 * 2) an external command by forking a child process.
 *
 * @param cmdline The command line string to be parsed and executed.
 *
 * @return void
 *
 * @details
 * - Parses cmdline into tokens using parseline()
 * - Built-in commands:
 *      quit: exits shell,
 *      jobs: lists jobs with I/O redirection support,
 *      bg/fg: resumes stopped jobs in background/foreground
 * - External commands:
 *      executes it via execve()
 * - Parent process manages foreground/background job execution:
 *      background jobs: prints job info and returns immediately
 *      foreground jobs: blocks until completion via sigsuspend()
 *
 * NOTE: The shell is supposed to be a long-running process, so this function
 *       (and its helpers) should avoid exiting on error.  This is not to say
 *       they shouldn't detect and print (or otherwise handle) errors!
 *
 */
void eval(const char *cmdline) {
    parseline_return parse_result;
    struct cmdline_tokens token;

    // Parse command line
    parse_result = parseline(cmdline, &token);

    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY) {
        return;
    }

    // Implement commands here.
    // builtin commands (quit, jobs, bg, fg)
    // quit
    if (token.builtin == BUILTIN_QUIT) {
        exit(0);
    }
    // jobs
    if (token.builtin == BUILTIN_JOBS) {
        // check and handle I/O redirection
        int fd = STDOUT_FILENO;
        if (token.outfile) {
            fd = open(token.outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
            if (fd < 0) {
                sio_eprintf("%s: %s\n", token.outfile, strerror(errno));
                return;
            }
        }
        // blocked signals(SIGCHLD, SIGINT, SIGTSTP) before accessing job list
        sigset_t mask, prev_mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTSTP);
        sigprocmask(SIG_BLOCK, &mask, &prev_mask);
        // Code Region that will not be interrupted by SIGINT
        list_jobs(fd);
        if (token.outfile) {
            close(fd);
        }
        // unblocked signals(SIGCHLD, SIGINT, SIGTSTP) after accessing job list
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return;
    }
    // bg/fg job
    if (token.builtin == BUILTIN_BG || token.builtin == BUILTIN_FG) {
        jid_t jid;
        pid_t pid;
        job_state state = (token.builtin == BUILTIN_BG) ? BG : FG;
        const char *bg_or_fg = token.builtin == BUILTIN_BG ? "bg" : "fg";
        char *arg_1 = token.argv[1];
        sigset_t mask, prev_mask;

        if (!arg_1) {
            sio_eprintf("%s command requires PID or %%jobid argument\n",
                        bg_or_fg);
            return;
        }

        // parse and check jid and pid
        if (arg_1[0] == '%') {
            jid = (jid_t)atoi(arg_1 + 1);
            if (jid <= 0) {
                sio_eprintf("%s: argument must be a PID or %%jobid\n",
                            bg_or_fg);
                return;
            }

            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigaddset(&mask, SIGINT);
            sigaddset(&mask, SIGTSTP);
            sigprocmask(SIG_BLOCK, &mask, &prev_mask);
            // critical section starts
            if (!job_exists(jid)) {
                sio_eprintf("%%%d: No such job\n", (int)jid);
                sigprocmask(SIG_SETMASK, &prev_mask, NULL);
                return;
            }
            pid = job_get_pid(jid);
            job_set_state(jid, state);
            // critical section ends
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        } else {
            pid = (pid_t)atoi(arg_1);
            if (pid <= 0) {
                sio_eprintf("%s: argument must be a PID or %%jobid\n",
                            bg_or_fg);
                return;
            }

            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigaddset(&mask, SIGINT);
            sigaddset(&mask, SIGTSTP);
            sigprocmask(SIG_BLOCK, &mask, &prev_mask);
            // critical section starts
            jid = job_from_pid(pid);
            if (jid == 0) {
                sio_eprintf("(%d): No such process\n", (int)pid);
                sigprocmask(SIG_SETMASK, &prev_mask, NULL);
                return;
            }
            job_set_state(jid, state);
            // critical section ends
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        }

        // restart by SIGCONT
        kill(-pid, SIGCONT);

        if (token.builtin == BUILTIN_BG) {
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigaddset(&mask, SIGINT);
            sigaddset(&mask, SIGTSTP);
            sigprocmask(SIG_BLOCK, &mask, &prev_mask);
            const char *jcmd = job_get_cmdline(jid);
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);

            sio_printf("[%d] (%d) %s\n", (int)jid, (int)pid, jcmd);
        }

        if (token.builtin == BUILTIN_FG) {
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigaddset(&mask, SIGINT);
            sigaddset(&mask, SIGTSTP);
            sigprocmask(SIG_BLOCK, &mask, &prev_mask);
            // wait async-safely and resource efficiently
            // until all FG children finish
            while (
                fg_job() !=
                0) // Return JID of current foreground job, or 0 if no such job
            {
                sigsuspend(&prev_mask);
            }
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        }
        return;
    }

    // external command
    sigset_t mask, prev_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);
    // fork a child process (blocked before adding a new child to job list)
    pid_t pid = fork();
    // fork error
    if (pid < 0) {
        perror("fork failed");
        // unblock signals and then return
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return;
    }
    // child
    if (pid == 0) {
        setpgid(0,
                0); // Set the child's process group ID to its own PID, separete
                    // it from parent process (shell's foreground) first

        // check and handle I/O redirection
        if (token.infile) {
            int fd_in = open(token.infile, O_RDONLY);
            if (fd_in < 0) {
                sio_eprintf("%s: %s\n", token.infile, strerror(errno));
                _exit(1);
            }
            if (dup2(fd_in, STDIN_FILENO) < 0) {
                sio_eprintf("%s: %s\n", token.infile, strerror(errno));
                _exit(1);
            }
            close(fd_in);
        }
        if (token.outfile) {
            int fd_out =
                open(token.outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
            if (fd_out < 0) {
                sio_eprintf("%s: %s\n", token.outfile, strerror(errno));
                _exit(1);
            }
            if (dup2(fd_out, STDOUT_FILENO) < 0) {
                sio_eprintf("%s: %s\n", token.outfile, strerror(errno));
                _exit(1);
            }
            close(fd_out);
        }

        // unblock signal, so child process can receive interruption (ex:
        // Ctrl+C/Z)
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        // run execve and check fail or not
        if (execve(token.argv[0], token.argv, environ) == -1) {
            sio_eprintf("%s: %s\n", token.argv[0], strerror(errno));
            _exit(1);
        }
    }
    // parent
    job_state state = (parse_result == PARSELINE_BG) ? BG : FG;
    jid_t jid = add_job(pid, state, cmdline); // add child to job_list
    // BG
    if (parse_result == PARSELINE_BG) {
        // print jid pid cammand, ex: [1] (32757) /bin/ls &
        // and then let the job running in BG and return to shell
        sio_printf("[%d] (%d) %s\n", (int)jid, (int)pid, cmdline);
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return;
    }
    // FG
    if (parse_result == PARSELINE_FG) {
        // wait async-safely and resource efficiently
        // until all FG children finish
        while (fg_job() !=
               0) // Return JID of current foreground job, or 0 if no such job
        {
            sigsuspend(
                &prev_mask); // replaces the current signal mask with prev_mask
        }
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }
}

/*****************
 * Signal handlers
 *****************/

/**
 * @brief Reap child processes that have terminated or stopped.
 *
 * Catch SIGCHLD signal from the kernal.
 * It happens when a child process terminates or stops.
 * Whenever that happens, reap child from the job_list or update its state.
 */
void sigchld_handler(int sig) {
    int old_errno = errno;
    sigset_t mask, prev_mask;
    pid_t pid;
    int child_status;

    // block signals
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    // critical section starts ======
    // reap all terminated or stopped children
    // terminated => delete it from job_list
    // stopped => change its state
    while ((pid = waitpid(-1, &child_status, WNOHANG | WUNTRACED)) >
           0) { // WNOHANG|WUNTRACED => return (PID of one of the stopped or
                // terminated children) || (0 if none).
        jid_t jid = job_from_pid(pid);
        // WIFEXITED(status): child terminated "normally"
        if (WIFEXITED(child_status)) {
            delete_job(jid); /*Delete the child from the job list*/
            continue;
        }
        // WIFSIGNALED(status): child terminated by SIGINT
        else if (WIFSIGNALED(child_status)) {
            delete_job(jid); /*Delete the child from the job list*/
            sio_printf("Job [%d] (%d) terminated by signal %d\n", (int)jid,
                       (int)pid, (int)WTERMSIG(child_status));
            continue;
        }
        // WIFSTOPPED(status): child stopped by SIGTSTP
        else if (WIFSTOPPED(child_status)) {
            job_set_state(jid, ST); /* Set job with jid to STOP state*/
            sio_printf("Job [%d] (%d) stopped by signal %d\n", (int)jid,
                       (int)pid, (int)WSTOPSIG(child_status));
            continue;
        }
    }
    // critical section ends ======

    // unblock signal and resume errno
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    errno = old_errno;
}

/**
 * @brief Kill all FG jobs in the same process group
 *
 * Catch SIGINT signal (Ctrl+C)
 * and forward it to all process in the same process group as the FG job
 */
void sigint_handler(int sig) {
    int old_errno = errno;
    sigset_t mask, prev_mask;

    // block signal
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    // critical section starts
    jid_t jid = fg_job();
    if (jid > 0) {
        // get process id
        pid_t pid = job_get_pid(jid);
        // forward signal to entire process group
        kill(-pid, sig);
    }
    // critical section ends

    // unblock signal
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    // restore global errno message
    errno = old_errno;
}

/**
 * @brief STOP FG jobs in the same process group
 *
 * Catch SIGTSTP signal (Ctrl+Z)
 * and forward it to all process in the same process group as the FG job
 */
void sigtstp_handler(int sig) {
    int old_errno = errno;
    sigset_t mask, prev_mask;

    // block signal
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    // critical section starts
    jid_t jid = fg_job();
    if (jid > 0) {
        // get process id
        pid_t pid = job_get_pid(jid);
        // forward signal to entire process group
        kill(-pid, sig);
    }
    // critical section ends

    // unblock signal
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);

    // restore global errno message
    errno = old_errno;
}

/**
 * @brief Attempt to clean up global resources when the program exits.
 *
 * In particular, the job list must be freed at this time, since it may
 * contain leftover buffers from existing or even deleted jobs.
 */
void cleanup(void) {
    // Signals handlers need to be removed before destroying the joblist
    Signal(SIGINT, SIG_DFL);  // Handles Ctrl-C
    Signal(SIGTSTP, SIG_DFL); // Handles Ctrl-Z
    Signal(SIGCHLD, SIG_DFL); // Handles terminated or stopped child

    destroy_job_list();
}
