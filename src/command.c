#include <stdlib.h>

#include "command.h"
#include "util.h"

#define INITIAL_ARGV_CAPACITY 8
#define INITIAL_REDIRECT_CAPACITY 4
#define INITIAL_PIPELINE_CAPACITY 4


SimpleCommand *simple_command_new(void) {
    SimpleCommand *cmd = xmalloc(sizeof(SimpleCommand));
    cmd->argv_capacity = INITIAL_ARGV_CAPACITY;
    cmd->argv = xmalloc(sizeof(char *) * (size_t)cmd->argv_capacity);
    cmd->argv[0] = NULL;
    cmd->argc = 0;
    cmd->redirect_capacity = INITIAL_REDIRECT_CAPACITY;
    cmd->redirects = xmalloc(sizeof(Redirection) * (size_t)cmd->redirect_capacity);
    cmd->num_redirects = 0;
    return cmd;
}

void simple_command_add_arg(SimpleCommand *cmd, const char *arg) {
    // +2: one for the new arg, one for the NULL terminator
    if (cmd->argc + 2 > cmd->argv_capacity) {
        cmd->argv_capacity *= 2;
        cmd->argv = xrealloc(cmd->argv,
                             sizeof(char *) * (size_t)cmd->argv_capacity);
    }
    cmd->argv[cmd->argc] = xstrdup(arg);
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;
}

void simple_command_add_redirect(SimpleCommand *cmd, RedirectType type,
                                 const char *target) {
    if (cmd->num_redirects >= cmd->redirect_capacity) {
        cmd->redirect_capacity *= 2;
        cmd->redirects = xrealloc(cmd->redirects,
                                  sizeof(Redirection) * (size_t)cmd->redirect_capacity);
    }
    cmd->redirects[cmd->num_redirects].type = type;
    cmd->redirects[cmd->num_redirects].target = xstrdup(target);
    cmd->num_redirects++;
}

void simple_command_free(SimpleCommand *cmd) {
    if (!cmd) {
        return;
    }
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    for (int i = 0; i < cmd->num_redirects; i++) {
        free(cmd->redirects[i].target);
    }
    free(cmd->redirects);
    free(cmd);
}

Pipeline *pipeline_new(void) {
    Pipeline *pl = xmalloc(sizeof(Pipeline));
    pl->cmd_capacity = INITIAL_PIPELINE_CAPACITY;
    pl->commands = xmalloc(sizeof(SimpleCommand *) * (size_t)pl->cmd_capacity);
    pl->num_commands = 0;
    pl->background = 0;
    pl->pipe_capacity = INITIAL_PIPELINE_CAPACITY;
    pl->pipe_types = xmalloc(sizeof(PipeType) * (size_t)pl->pipe_capacity);
    return pl;
}

void pipeline_add_command(Pipeline *pl, SimpleCommand *cmd) {
    if (pl->num_commands >= pl->cmd_capacity) {
        pl->cmd_capacity *= 2;
        pl->commands = xrealloc(pl->commands,
                                sizeof(SimpleCommand *) * (size_t)pl->cmd_capacity);
    }
    pl->commands[pl->num_commands++] = cmd;
}

void pipeline_add_pipe_type(Pipeline *pl, PipeType type) {
    int idx = pl->num_commands - 2; // pipe between cmd[n-2] and cmd[n-1]
    if (idx < 0) {
        return;
    }
    if (idx >= pl->pipe_capacity) {
        pl->pipe_capacity *= 2;
        pl->pipe_types = xrealloc(pl->pipe_types,
                                   sizeof(PipeType) * (size_t)pl->pipe_capacity);
    }
    pl->pipe_types[idx] = type;
}

void pipeline_free(Pipeline *pl) {
    if (!pl) {
        return;
    }
    for (int i = 0; i < pl->num_commands; i++) {
        simple_command_free(pl->commands[i]);
    }
    free(pl->commands);
    free(pl->pipe_types);
    free(pl);
}

CommandList *command_list_new(void) {
    CommandList *list = xmalloc(sizeof(CommandList));
    list->pipeline_capacity = INITIAL_PIPELINE_CAPACITY;
    list->pipelines = xmalloc(sizeof(Pipeline *) * (size_t)list->pipeline_capacity);
    list->operators = xmalloc(sizeof(ListOpType) * (size_t)list->pipeline_capacity);
    list->num_pipelines = 0;
    return list;
}

void command_list_add_pipeline(CommandList *list, Pipeline *pl) {
    if (list->num_pipelines >= list->pipeline_capacity) {
        list->pipeline_capacity *= 2;
        list->pipelines = xrealloc(list->pipelines,
                                   sizeof(Pipeline *) * (size_t)list->pipeline_capacity);
        list->operators = xrealloc(list->operators,
                                   sizeof(ListOpType) * (size_t)list->pipeline_capacity);
    }
    list->pipelines[list->num_pipelines++] = pl;
}

void command_list_add_operator(CommandList *list, ListOpType op) {
    int idx = list->num_pipelines - 2;
    if (idx < 0) {
        return;
    }
    list->operators[idx] = op;
}

void command_list_free(CommandList *list) {
    if (!list) {
        return;
    }
    for (int i = 0; i < list->num_pipelines; i++) {
        pipeline_free(list->pipelines[i]);
    }
    free(list->pipelines);
    free(list->operators);
    free(list);
}
