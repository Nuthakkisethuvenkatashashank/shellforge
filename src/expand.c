#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expand.h"

void expand_variables(pipeline_t *pipeline)
{
    for (int i = 0; i < pipeline->command_count; i++)
    {
        command_t *cmd = &pipeline->commands[i];

        for (int j = 0; j < cmd->argc; j++)
        {
            char *arg = cmd->argv[j];

            if (arg == NULL)
                continue;

            if (arg[0] == '$')
            {
                char *name = arg + 1;
                char *slash = strchr(name, '/');

                if (slash != NULL)
                {
                    char variable[256];
                    size_t len = slash - name;

                    if (len >= sizeof(variable))
                        continue;

                    strncpy(variable, name, len);
                    variable[len] = '\0';

                    char *value = getenv(variable);

                    if (value != NULL)
                    {
                        size_t new_len =
                            strlen(value) + strlen(slash) + 1;

                        char *expanded =
                            malloc(new_len);

                        if (expanded != NULL)
                        {
                            strcpy(expanded, value);
                            strcat(expanded, slash);

                            cmd->argv[j] = expanded;
                        }
                    }
                }
                else
                {
                    char *value = getenv(name);

                    if (value != NULL)
                        cmd->argv[j] = value;
                    else
                        cmd->argv[j] = "";
                }
            }
        }
    }
}
