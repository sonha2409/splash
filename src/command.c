#include <stdlib.h>

#include "command.h"
#include "util.h"

#define INITIAL_ARGV_CAPACITY 8
#define INITIAL_REDIRECT_CAPACITY 4
#define INITIAL_CAPACITY 4


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
    pl->cmd_capacity = INITIAL_CAPACITY;
    pl->commands = xmalloc(sizeof(SimpleCommand *) * (size_t)pl->cmd_capacity);
    pl->num_commands = 0;
    pl->background = 0;
    pl->pipe_capacity = INITIAL_CAPACITY;
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

void node_free(Node *node) {
    if (!node) {
        return;
    }
    switch (node->type) {
        case NODE_PIPELINE:
            pipeline_free(node->pipeline);
            break;
        case NODE_IF:
            if_command_free(node->if_cmd);
            break;
    }
}

// Grow the entries/operators arrays if needed.
static void command_list_grow(CommandList *list) {
    if (list->num_entries >= list->capacity) {
        list->capacity *= 2;
        list->entries = xrealloc(list->entries,
                                 sizeof(Node) * (size_t)list->capacity);
        list->operators = xrealloc(list->operators,
                                   sizeof(ListOpType) * (size_t)list->capacity);
    }
}

CommandList *command_list_new(void) {
    CommandList *list = xmalloc(sizeof(CommandList));
    list->capacity = INITIAL_CAPACITY;
    list->entries = xmalloc(sizeof(Node) * (size_t)list->capacity);
    list->operators = xmalloc(sizeof(ListOpType) * (size_t)list->capacity);
    list->num_entries = 0;
    return list;
}

void command_list_add_pipeline(CommandList *list, Pipeline *pl) {
    command_list_grow(list);
    list->entries[list->num_entries].type = NODE_PIPELINE;
    list->entries[list->num_entries].pipeline = pl;
    list->num_entries++;
}

void command_list_add_if(CommandList *list, IfCommand *if_cmd) {
    command_list_grow(list);
    list->entries[list->num_entries].type = NODE_IF;
    list->entries[list->num_entries].if_cmd = if_cmd;
    list->num_entries++;
}

void command_list_add_operator(CommandList *list, ListOpType op) {
    int idx = list->num_entries - 2;
    if (idx < 0) {
        return;
    }
    list->operators[idx] = op;
}

void command_list_free(CommandList *list) {
    if (!list) {
        return;
    }
    for (int i = 0; i < list->num_entries; i++) {
        node_free(&list->entries[i]);
    }
    free(list->entries);
    free(list->operators);
    free(list);
}

IfCommand *if_command_new(void) {
    IfCommand *cmd = xmalloc(sizeof(IfCommand));
    cmd->clause_capacity = INITIAL_CAPACITY;
    cmd->clauses = xmalloc(sizeof(IfClause) * (size_t)cmd->clause_capacity);
    cmd->num_clauses = 0;
    cmd->else_body = NULL;
    return cmd;
}

void if_command_add_clause(IfCommand *cmd, CommandList *condition,
                           CommandList *body) {
    if (cmd->num_clauses >= cmd->clause_capacity) {
        cmd->clause_capacity *= 2;
        cmd->clauses = xrealloc(cmd->clauses,
                                sizeof(IfClause) * (size_t)cmd->clause_capacity);
    }
    cmd->clauses[cmd->num_clauses].condition = condition;
    cmd->clauses[cmd->num_clauses].body = body;
    cmd->num_clauses++;
}

void if_command_free(IfCommand *cmd) {
    if (!cmd) {
        return;
    }
    for (int i = 0; i < cmd->num_clauses; i++) {
        command_list_free(cmd->clauses[i].condition);
        command_list_free(cmd->clauses[i].body);
    }
    free(cmd->clauses);
    command_list_free(cmd->else_body);
    free(cmd);
}
