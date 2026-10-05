#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "executor.h"
#include "builtin.h"


/* =====================================================
   BACKGROUND PROCESS HANDLER
   ===================================================== */

void reap_background_processes(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        printf("\n[Background process %d completed]\n", pid);
        fflush(stdout);
    }
}


void setup_background_handler(void)
{
    /*
     * Keep SIGCHLD as default so foreground processes
     * can be waited for normally.
     */
    signal(SIGCHLD, SIG_DFL);
}


/* =====================================================
   EXECUTE SINGLE COMMAND
   ===================================================== */

int execute_command(command_t *cmd)
{
    pid_t pid;
    int status;


    /* =================================================
       BUILTIN COMMAND
       ================================================= */

    if (is_builtin(cmd))
    {
        /*
         * cd must execute in the parent shell,
         * otherwise the directory change would only
         * affect the child process.
         */
        if (strcmp(cmd->argv[0], "cd") == 0)
        {
            return execute_builtin(cmd);
        }


        /*
         * Other builtins such as echo and pwd are
         * executed in a child so that redirection works.
         */
        pid = fork();

        if (pid < 0)
        {
            perror("fork");
            return -1;
        }


        /* ---------------------------------------------
           BUILTIN CHILD
           --------------------------------------------- */

        if (pid == 0)
        {
            /*
             * Input redirection
             */
            if (cmd->input[0] != '\0')
            {
                int fd = open(cmd->input, O_RDONLY);

                if (fd < 0)
                {
                    perror("open input");
                    exit(EXIT_FAILURE);
                }

                if (dup2(fd, STDIN_FILENO) < 0)
                {
                    perror("dup2");
                    close(fd);
                    exit(EXIT_FAILURE);
                }

                close(fd);
            }


            /*
             * Output redirection
             */
            if (cmd->output[0] != '\0')
            {
                int fd;

                if (cmd->append)
                {
                    fd = open(cmd->output,
                              O_WRONLY | O_CREAT | O_APPEND,
                              0644);
                }
                else
                {
                    fd = open(cmd->output,
                              O_WRONLY | O_CREAT | O_TRUNC,
                              0644);
                }

                if (fd < 0)
                {
                    perror("open output");
                    exit(EXIT_FAILURE);
                }

                if (dup2(fd, STDOUT_FILENO) < 0)
                {
                    perror("dup2");
                    close(fd);
                    exit(EXIT_FAILURE);
                }

                close(fd);
            }


            /*
             * Execute builtin
             */
            execute_builtin(cmd);

            exit(EXIT_SUCCESS);
        }


        /* ---------------------------------------------
           BUILTIN BACKGROUND
           --------------------------------------------- */

        if (cmd->background)
        {
            printf("[Background process %d started]\n", pid);
            fflush(stdout);

            return 0;
        }


        /* ---------------------------------------------
           BUILTIN FOREGROUND
           --------------------------------------------- */

        if (waitpid(pid, &status, 0) < 0)
        {
            perror("waitpid");
            return -1;
        }

        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }

        return -1;
    }


    /* =================================================
       EXTERNAL COMMAND
       ================================================= */

    pid = fork();

    if (pid < 0)
    {
        perror("fork");
        return -1;
    }


    /* ---------------------------------------------
       EXTERNAL COMMAND CHILD
       --------------------------------------------- */

    if (pid == 0)
    {
        /*
         * Input redirection
         */
        if (cmd->input[0] != '\0')
        {
            int fd = open(cmd->input, O_RDONLY);

            if (fd < 0)
            {
                perror("open input");
                exit(EXIT_FAILURE);
            }

            if (dup2(fd, STDIN_FILENO) < 0)
            {
                perror("dup2");
                close(fd);
                exit(EXIT_FAILURE);
            }

            close(fd);
        }


        /*
         * Output redirection
         */
        if (cmd->output[0] != '\0')
        {
            int fd;

            if (cmd->append)
            {
                fd = open(cmd->output,
                          O_WRONLY | O_CREAT | O_APPEND,
                          0644);
            }
            else
            {
                fd = open(cmd->output,
                          O_WRONLY | O_CREAT | O_TRUNC,
                          0644);
            }

            if (fd < 0)
            {
                perror("open output");
                exit(EXIT_FAILURE);
            }

            if (dup2(fd, STDOUT_FILENO) < 0)
            {
                perror("dup2");
                close(fd);
                exit(EXIT_FAILURE);
            }

            close(fd);
        }


        /*
         * Execute external command
         */
        execvp(cmd->argv[0], cmd->argv);

        perror("execvp");
        exit(EXIT_FAILURE);
    }


    /* =================================================
       EXTERNAL BACKGROUND PROCESS
       ================================================= */

    if (cmd->background)
    {
        printf("[Background process %d started]\n", pid);
        fflush(stdout);

        return 0;
    }


    /* =================================================
       EXTERNAL FOREGROUND PROCESS
       ================================================= */

    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
        return -1;
    }


    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }

    return -1;
}


/* =====================================================
   EXECUTE PIPELINE
   ===================================================== */

int execute_pipeline(pipeline_t *pipeline)
{
    int command_count = pipeline->command_count;

    int i;
    int status;
    int final_status = 0;

    pid_t pids[command_count];

    int pipes[command_count - 1][2];


    /* =================================================
       SINGLE COMMAND
       ================================================= */

    if (command_count == 1)
    {
        return execute_command(&pipeline->commands[0]);
    }


    /* =================================================
       CREATE PIPES
       ================================================= */

    for (i = 0; i < command_count - 1; i++)
    {
        if (pipe(pipes[i]) < 0)
        {
            perror("pipe");
            return -1;
        }
    }


    /* =================================================
       CREATE PROCESSES
       ================================================= */

    for (i = 0; i < command_count; i++)
    {
        pids[i] = fork();

        if (pids[i] < 0)
        {
            perror("fork");
            return -1;
        }


        /* ---------------------------------------------
           CHILD PROCESS
           --------------------------------------------- */

        if (pids[i] == 0)
        {
            /*
             * Input from previous pipe
             */
            if (i > 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }


            /*
             * Output to next pipe
             */
            if (i < command_count - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }


            /*
             * Input redirection
             */
            if (pipeline->commands[i].input[0] != '\0')
            {
                int fd = open(
                    pipeline->commands[i].input,
                    O_RDONLY
                );

                if (fd < 0)
                {
                    perror("open input");
                    exit(EXIT_FAILURE);
                }

                if (dup2(fd, STDIN_FILENO) < 0)
                {
                    perror("dup2");
                    close(fd);
                    exit(EXIT_FAILURE);
                }

                close(fd);
            }


            /*
             * Output redirection
             */
            if (pipeline->commands[i].output[0] != '\0')
            {
                int fd;

                if (pipeline->commands[i].append)
                {
                    fd = open(
                        pipeline->commands[i].output,
                        O_WRONLY | O_CREAT | O_APPEND,
                        0644
                    );
                }
                else
                {
                    fd = open(
                        pipeline->commands[i].output,
                        O_WRONLY | O_CREAT | O_TRUNC,
                        0644
                    );
                }

                if (fd < 0)
                {
                    perror("open output");
                    exit(EXIT_FAILURE);
                }

                if (dup2(fd, STDOUT_FILENO) < 0)
                {
                    perror("dup2");
                    close(fd);
                    exit(EXIT_FAILURE);
                }

                close(fd);
            }


            /*
             * Close all pipe descriptors
             */
            for (int j = 0; j < command_count - 1; j++)
            {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }


            /*
             * Execute command
             */
            execvp(
                pipeline->commands[i].argv[0],
                pipeline->commands[i].argv
            );

            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }


    /* =================================================
       PARENT PROCESS

       Close all pipes.
       ================================================= */

    for (i = 0; i < command_count - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }


    /* =================================================
       BACKGROUND PIPELINE
       ================================================= */

    if (pipeline->commands[command_count - 1].background)
    {
        printf("[Background pipeline started: PID %d]\n",
               pids[command_count - 1]);

        fflush(stdout);

        return 0;
    }


    /* =====================================================
       FOREGROUND PIPELINE

       Wait for every process.
       ===================================================== */

    for (i = 0; i < command_count; i++)
    {
        status = 0;


        if (waitpid(pids[i], &status, 0) < 0)
        {
            perror("waitpid");

            continue;
        }


        /*
         * Pipeline exit status is normally taken from
         * the last command.
         */

        if (i == command_count - 1)
        {
            if (WIFEXITED(status))
            {
                final_status =
                    WEXITSTATUS(status);
            }

            else
            {
                final_status = -1;
            }
        }
    }


    return final_status;
}
