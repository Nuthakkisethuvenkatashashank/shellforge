#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <signal.h>

#include "parser.h"
#include "executor.h"
#include "builtin.h"
#include "jobs.h"
#include "job_control.h"


/* =========================================================
   SIGCHLD HANDLER
   ========================================================= */

static void sigchld_handler(int sig)
{
    int saved_errno = errno;
    pid_t pid;

    (void)sig;

    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
    {
        job_t *job;

        job = job_find_by_pgid(pid);

        if (job != NULL)
        {
            job_done(pid);
        }
    }

    errno = saved_errno;
}


/* =========================================================
   SETUP SIGCHLD HANDLER
   ========================================================= */

void setup_background_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = sigchld_handler;

    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) < 0)
    {
        perror("sigaction");
    }
}


/* =========================================================
   BLOCK SIGCHLD
   ========================================================= */

static int block_sigchld(sigset_t *old_mask)
{
    sigset_t set;

    sigemptyset(&set);

    sigaddset(&set, SIGCHLD);

    if (sigprocmask(
            SIG_BLOCK,
            &set,
            old_mask
        ) < 0)
    {
        perror("sigprocmask");

        return -1;
    }

    return 0;
}


/* =========================================================
   RESTORE SIGNAL MASK
   ========================================================= */

static void restore_signal_mask(
    const sigset_t *old_mask
)
{
    if (sigprocmask(
            SIG_SETMASK,
            old_mask,
            NULL
        ) < 0)
    {
        perror("sigprocmask");
    }
}


/* =========================================================
   APPLY REDIRECTION
   ========================================================= */

static int apply_redirection(command_t *cmd)
{
    int fd;


    /* INPUT < */

    if (cmd->input[0] != '\0')
    {
        fd = open(
            cmd->input,
            O_RDONLY
        );

        if (fd < 0)
        {
            perror(cmd->input);

            return -1;
        }

        if (dup2(
                fd,
                STDIN_FILENO
            ) < 0)
        {
            perror("dup2 input");

            close(fd);

            return -1;
        }

        close(fd);
    }


    /* OUTPUT > or >> */

    if (cmd->output[0] != '\0')
    {
        if (cmd->append)
        {
            fd = open(
                cmd->output,
                O_WRONLY |
                O_CREAT |
                O_APPEND,
                0644
            );
        }
        else
        {
            fd = open(
                cmd->output,
                O_WRONLY |
                O_CREAT |
                O_TRUNC,
                0644
            );
        }


        if (fd < 0)
        {
            perror(cmd->output);

            return -1;
        }


        if (dup2(
                fd,
                STDOUT_FILENO
            ) < 0)
        {
            perror("dup2 output");

            close(fd);

            return -1;
        }

        close(fd);
    }


    return 0;
}


/* =========================================================
   PREPARE ARGUMENTS
   ========================================================= */

static void prepare_arguments(
    command_t *cmd,
    char *args[]
)
{
    int i;

    for (i = 0;
         i < cmd->argc;
         i++)
    {
        args[i] = cmd->argv[i];
    }

    args[cmd->argc] = NULL;
}


/* =========================================================
   EXECUTE BUILTIN WITH REDIRECTION
   ========================================================= */

static int execute_builtin_with_redirection(
    command_t *cmd
)
{
    int saved_stdin;
    int saved_stdout;

    int result;


    saved_stdin = dup(STDIN_FILENO);

    if (saved_stdin < 0)
    {
        perror("dup stdin");

        return -1;
    }


    saved_stdout = dup(STDOUT_FILENO);

    if (saved_stdout < 0)
    {
        perror("dup stdout");

        close(saved_stdin);

        return -1;
    }


    if (apply_redirection(cmd) < 0)
    {
        close(saved_stdin);

        close(saved_stdout);

        return -1;
    }


    result = execute_builtin(cmd);


    if (dup2(
            saved_stdin,
            STDIN_FILENO
        ) < 0)
    {
        perror("restore stdin");
    }


    if (dup2(
            saved_stdout,
            STDOUT_FILENO
        ) < 0)
    {
        perror("restore stdout");
    }


    close(saved_stdin);

    close(saved_stdout);


    return result;
}


/* =========================================================
   EXECUTE SINGLE COMMAND
   ========================================================= */

int execute_command(command_t *cmd)
{
    pid_t pid;

    int status;

    sigset_t old_mask;


    if (cmd == NULL)
    {
        return -1;
    }


    if (cmd->argc == 0)
    {
        return 0;
    }


    /* FOREGROUND BUILTIN */

    if (is_builtin(cmd) &&
        !cmd->background)
    {
        return execute_builtin_with_redirection(cmd);
    }


    /* BLOCK SIGCHLD */

    if (block_sigchld(
            &old_mask
        ) < 0)
    {
        return -1;
    }


    /* FORK */

    pid = fork();


    if (pid < 0)
    {
        perror("fork");

        restore_signal_mask(
            &old_mask
        );

        return -1;
    }


    /* =====================================================
       CHILD
       ===================================================== */

    if (pid == 0)
    {
        char *args[MAX_ARGS + 1];


        restore_signal_mask(
            &old_mask
        );


        /* Child gets its own process group */

        if (setpgid(
                0,
                0
            ) < 0)
        {
            perror("setpgid");
        }


        /* Restore default signals */

        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);


        /* Redirection */

        if (apply_redirection(cmd) < 0)
        {
            _exit(EXIT_FAILURE);
        }


        prepare_arguments(
            cmd,
            args
        );


        /* Builtin */

        if (is_builtin(cmd))
        {
            int result;

            result =
                execute_builtin(cmd);

            _exit(result);
        }


        /* External command */

        execvp(
            args[0],
            args
        );


        fprintf(
            stderr,
            "shellforge: %s: command not found\n",
            args[0]
        );

        _exit(127);
    }


    /* =====================================================
       PARENT
       ===================================================== */

    if (setpgid(
            pid,
            pid
        ) < 0)
    {
        if (errno != EACCES &&
            errno != ESRCH)
        {
            perror("setpgid");
        }
    }


    /* =====================================================
       BACKGROUND
       ===================================================== */

    if (cmd->background)
    {
        int job_id;


        job_id =
            job_add(
                pid,
                cmd->argv[0],
                JOB_RUNNING
            );


        restore_signal_mask(
            &old_mask
        );


        if (job_id > 0)
        {
            printf(
                "[%d] %d\n",
                job_id,
                pid
            );

            fflush(stdout);
        }


        return 0;
    }


    /* =====================================================
       FOREGROUND
       ===================================================== */

    give_terminal_to(pid);


    restore_signal_mask(
        &old_mask
    );


    /*
     * Wait for exit or Ctrl+Z.
     */

    if (waitpid(
            -pid,
            &status,
            WUNTRACED
        ) < 0)
    {
        perror("waitpid");

        take_terminal_back();

        return -1;
    }


    /*
     * Return terminal to Shellforge.
     */

    take_terminal_back();


    /* =====================================================
       STOPPED
       ===================================================== */

    if (WIFSTOPPED(status))
    {
        int job_id;


        job_id =
            job_add(
                pid,
                cmd->argv[0],
                JOB_STOPPED
            );


        if (job_id > 0)
        {
            printf(
                "\n[%d]+  Stopped    %s\n",
                job_id,
                cmd->argv[0]
            );

            fflush(stdout);
        }


        return 0;
    }


    /* =====================================================
       EXITED
       ===================================================== */

    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }


    /* =====================================================
       SIGNALED
       ===================================================== */

    if (WIFSIGNALED(status))
    {
        return 128 +
               WTERMSIG(status);
    }


    return -1;
}


/* =========================================================
   EXECUTE PIPELINE
   ========================================================= */

int execute_pipeline(
    pipeline_t *pipeline
)
{
    int command_count;

    int previous_read = -1;

    pid_t pids[MAX_COMMANDS];

    int status;

    int final_status = 0;

    int i;

    int background;

    sigset_t old_mask;


    if (pipeline == NULL)
    {
        return -1;
    }


    command_count =
        pipeline->command_count;


    if (command_count <= 0)
    {
        return 0;
    }


    background =
        pipeline
            ->commands[
                command_count - 1
            ]
            .background;


    /* SINGLE COMMAND */

    if (command_count == 1)
    {
        return execute_command(
            &pipeline->commands[0]
        );
    }


    /* BLOCK SIGCHLD */

    if (!background)
    {
        if (block_sigchld(
                &old_mask
            ) < 0)
        {
            return -1;
        }
    }


    /* =====================================================
       CREATE PIPELINE
       ===================================================== */

    for (i = 0;
         i < command_count;
         i++)
    {
        int pipefd[2];


        if (i < command_count - 1)
        {
            if (pipe(pipefd) < 0)
            {
                perror("pipe");

                if (!background)
                {
                    restore_signal_mask(
                        &old_mask
                    );
                }

                return -1;
            }
        }


        pids[i] = fork();


        if (pids[i] < 0)
        {
            perror("fork");

            if (!background)
            {
                restore_signal_mask(
                    &old_mask
                );
            }

            return -1;
        }


        /* =================================================
           CHILD
           ================================================= */

        if (pids[i] == 0)
        {
            command_t *cmd;

            char *args[MAX_ARGS + 1];

            int j;


            cmd =
                &pipeline->commands[i];


            /* Process group */

            if (i == 0)
            {
                setpgid(
                    0,
                    0
                );
            }
            else
            {
                setpgid(
                    0,
                    pids[0]
                );
            }


            if (!background)
            {
                restore_signal_mask(
                    &old_mask
                );
            }


            /* Restore default signals */

            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);


            /* INPUT PIPE */

            if (previous_read != -1)
            {
                if (dup2(
                        previous_read,
                        STDIN_FILENO
                    ) < 0)
                {
                    perror(
                        "dup2 previous pipe"
                    );

                    _exit(EXIT_FAILURE);
                }
            }


            /* OUTPUT PIPE */

            if (i < command_count - 1)
            {
                if (dup2(
                        pipefd[1],
                        STDOUT_FILENO
                    ) < 0)
                {
                    perror(
                        "dup2 next pipe"
                    );

                    _exit(EXIT_FAILURE);
                }
            }


            /* CLOSE PREVIOUS */

            if (previous_read != -1)
            {
                close(previous_read);
            }


            /* CLOSE CURRENT */

            if (i < command_count - 1)
            {
                close(pipefd[0]);

                close(pipefd[1]);
            }


            /* REDIRECTION */

            if (apply_redirection(cmd) < 0)
            {
                _exit(EXIT_FAILURE);
            }


            /* ARGUMENTS */

            for (j = 0;
                 j < cmd->argc;
                 j++)
            {
                args[j] =
                    cmd->argv[j];
            }

            args[cmd->argc] =
                NULL;


            /* BUILTIN */

            if (is_builtin(cmd))
            {
                int result;

                result =
                    execute_builtin(cmd);

                _exit(result);
            }


            /* EXTERNAL */

            execvp(
                args[0],
                args
            );


            fprintf(
                stderr,
                "shellforge: %s: command not found\n",
                args[0]
            );

            _exit(127);
        }


        /* =================================================
           PARENT
           ================================================= */

        if (i == 0)
        {
            setpgid(
                pids[i],
                pids[i]
            );
        }
        else
        {
            setpgid(
                pids[i],
                pids[0]
            );
        }


        /* CLOSE PREVIOUS */

        if (previous_read != -1)
        {
            close(previous_read);

            previous_read = -1;
        }


        /* SAVE NEXT READ */

        if (i < command_count - 1)
        {
            close(pipefd[1]);

            previous_read =
                pipefd[0];
        }
    }


    /* =====================================================
       BACKGROUND PIPELINE
       ===================================================== */

    if (background)
    {
        int job_id;

        char command_string[MAX_JOB_COMMAND];


        command_string[0] =
            '\0';


        for (i = 0;
             i < command_count;
             i++)
        {
            command_t *cmd;

            cmd =
                &pipeline->commands[i];


            if (i > 0)
            {
                strncat(
                    command_string,
                    " | ",
                    sizeof(command_string)
                    -
                    strlen(command_string)
                    - 1
                );
            }


            if (cmd->argc > 0)
            {
                strncat(
                    command_string,
                    cmd->argv[0],
                    sizeof(command_string)
                    -
                    strlen(command_string)
                    - 1
                );
            }
        }


        job_id =
            job_add(
                pids[0],
                command_string,
                JOB_RUNNING
            );


        if (job_id > 0)
        {
            printf(
                "[%d] %d\n",
                job_id,
                pids[0]
            );

            fflush(stdout);
        }


        return 0;
    }


    /* =====================================================
       FOREGROUND PIPELINE
       ===================================================== */

    give_terminal_to(
        pids[0]
    );


    restore_signal_mask(
        &old_mask
    );


    for (i = 0;
         i < command_count;
         i++)
    {
        status = 0;


        if (waitpid(
                pids[i],
                &status,
                WUNTRACED
            ) < 0)
        {
            perror("waitpid");

            continue;
        }


        if (i == command_count - 1)
        {
            if (WIFEXITED(status))
            {
                final_status =
                    WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                final_status =
                    128 +
                    WTERMSIG(status);
            }
            else if (WIFSTOPPED(status))
            {
                int job_id;


                job_id =
                    job_add(
                        pids[0],
                        "pipeline",
                        JOB_STOPPED
                    );


                if (job_id > 0)
                {
                    printf(
                        "\n[%d]+  Stopped    pipeline\n",
                        job_id
                    );

                    fflush(stdout);
                }


                final_status = 0;
            }
        }
    }


    take_terminal_back();


    return final_status;
}
