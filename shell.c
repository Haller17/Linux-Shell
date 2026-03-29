
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define MAX_LINE 1024
#define MAX_TOKENS 128
#define MAX_CMDS 32

typedef struct {
    char *argv[MAX_TOKENS];
    char *input_file;
    char *output_file;
} Command;

void trim_newline(char *line) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }
}

int tokenize(char *line, char *tokens[]) {
    int count = 0;
    char *token = strtok(line, " ");
    while (token != NULL && count < MAX_TOKENS - 1) {
        tokens[count++] = token;
        token = strtok(NULL, " ");
    }
    tokens[count] = NULL;
    return count;
}

int parse_commands(char *tokens[], int token_count, Command commands[]) {
    int cmd_index = 0;
    int arg_index = 0;

    commands[cmd_index].input_file = NULL;
    commands[cmd_index].output_file = NULL;

    for (int i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            commands[cmd_index].argv[arg_index] = NULL;
            cmd_index++;
            arg_index = 0;
            commands[cmd_index].input_file = NULL;
            commands[cmd_index].output_file = NULL;
        } 
        else if (strcmp(tokens[i], "<") == 0) {
            if (i + 1 < token_count) {
                commands[cmd_index].input_file = tokens[++i];
            }
        } 
        else if (strcmp(tokens[i], ">") == 0) {
            if (i + 1 < token_count) {
                commands[cmd_index].output_file = tokens[++i];
            }
        } 
        else {
            commands[cmd_index].argv[arg_index++] = tokens[i];
        }
    }

    commands[cmd_index].argv[arg_index] = NULL;
    return cmd_index + 1;
}

int handle_internal(Command *cmd) {
    if (cmd->argv[0] == NULL) {
        return 1;
    }

    if (strcmp(cmd->argv[0], "exit") == 0) {
        exit(0);
    }

    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (cmd->argv[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else {
            if (chdir(cmd->argv[1]) != 0) {
                perror("cd");
            }
        }
        return 1;
    }

    if (strcmp(cmd->argv[0], "time") == 0) {
        time_t now = time(NULL);
        if (now == (time_t)-1) {
            perror("time");
        } else {
            printf("%s", ctime(&now));
        }
        return 1;
    }

    return 0;
}

void execute_single_command(Command *cmd) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        if (cmd->input_file != NULL) {
            int fd_in = open(cmd->input_file, O_RDONLY);
            if (fd_in < 0) {
                perror("open input");
                exit(1);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (cmd->output_file != NULL) {
            int fd_out = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) {
                perror("open output");
                exit(1);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        execv(cmd->argv[0], cmd->argv);
        perror("execv");
        exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

void execute_piped_commands(Command commands[], int num_cmds) {
    int pipes[MAX_CMDS - 1][2];

    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return;
        }
    }

    for (int i = 0; i < num_cmds; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return;
        }

        if (pid == 0) {
            if (i == 0 && commands[i].input_file != NULL) {
                int fd_in = open(commands[i].input_file, O_RDONLY);
                if (fd_in < 0) {
                    perror("open input");
                    exit(1);
                }
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            }

            if (i == num_cmds - 1 && commands[i].output_file != NULL) {
                int fd_out = open(commands[i].output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) {
                    perror("open output");
                    exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }

            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }

            if (i < num_cmds - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execv(commands[i].argv[0], commands[i].argv);
            perror("execv");
            exit(1);
        }
    }

    for (int i = 0; i < num_cmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < num_cmds; i++) {
        wait(NULL);
    }
}

int main() {
    char line[MAX_LINE];
    char *tokens[MAX_TOKENS];
    Command commands[MAX_CMDS];

    while (1) {
        printf("CS340Shell: ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        trim_newline(line);

        if (strlen(line) == 0) {
            continue;
        }

        int token_count = tokenize(line, tokens);
        if (token_count == 0) {
            continue;
        }

        for (int i = 0; i < MAX_CMDS; i++) {
            commands[i].input_file = NULL;
            commands[i].output_file = NULL;
            for (int j = 0; j < MAX_TOKENS; j++) {
                commands[i].argv[j] = NULL;
            }
        }

        int num_cmds = parse_commands(tokens, token_count, commands);

        if (num_cmds == 1 && handle_internal(&commands[0])) {
            continue;
        }

        if (num_cmds == 1) {
            execute_single_command(&commands[0]);
        } else {
            execute_piped_commands(commands, num_cmds);
        }
    }

    return 0;
}