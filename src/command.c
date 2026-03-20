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
        case NODE_FOR:
            for_command_free(node->for_cmd);
            break;
        case NODE_WHILE:
            while_command_free(node->while_cmd);
            break;
        case NODE_CASE:
            case_command_free(node->case_cmd);
            break;
        case NODE_FUNCTION_DEF:
            function_def_free(node->func_def);
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

void command_list_add_for(CommandList *list, ForCommand *for_cmd) {
    command_list_grow(list);
    list->entries[list->num_entries].type = NODE_FOR;
    list->entries[list->num_entries].for_cmd = for_cmd;
    list->num_entries++;
}

void command_list_add_while(CommandList *list, WhileCommand *while_cmd) {
    command_list_grow(list);
    list->entries[list->num_entries].type = NODE_WHILE;
    list->entries[list->num_entries].while_cmd = while_cmd;
    list->num_entries++;
}

void command_list_add_case(CommandList *list, CaseCommand *case_cmd) {
    command_list_grow(list);
    list->entries[list->num_entries].type = NODE_CASE;
    list->entries[list->num_entries].case_cmd = case_cmd;
    list->num_entries++;
}

void command_list_add_function_def(CommandList *list, FunctionDef *def) {
    command_list_grow(list);
    list->entries[list->num_entries].type = NODE_FUNCTION_DEF;
    list->entries[list->num_entries].func_def = def;
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

ForCommand *for_command_new(const char *var_name) {
    ForCommand *cmd = xmalloc(sizeof(ForCommand));
    cmd->var_name = xstrdup(var_name);
    cmd->word_capacity = INITIAL_CAPACITY;
    cmd->words = xmalloc(sizeof(char *) * (size_t)cmd->word_capacity);
    cmd->num_words = 0;
    cmd->body_src = NULL;
    return cmd;
}

void for_command_add_word(ForCommand *cmd, const char *word) {
    if (cmd->num_words >= cmd->word_capacity) {
        cmd->word_capacity *= 2;
        cmd->words = xrealloc(cmd->words,
                              sizeof(char *) * (size_t)cmd->word_capacity);
    }
    cmd->words[cmd->num_words++] = xstrdup(word);
}

void for_command_free(ForCommand *cmd) {
    if (!cmd) {
        return;
    }
    free(cmd->var_name);
    for (int i = 0; i < cmd->num_words; i++) {
        free(cmd->words[i]);
    }
    free(cmd->words);
    free(cmd->body_src);
    free(cmd);
}

CaseCommand *case_command_new(const char *word) {
    CaseCommand *cmd = xmalloc(sizeof(CaseCommand));
    cmd->word = xstrdup(word);
    cmd->clause_capacity = INITIAL_CAPACITY;
    cmd->clauses = xmalloc(sizeof(CaseClause) * (size_t)cmd->clause_capacity);
    cmd->num_clauses = 0;
    return cmd;
}

CaseClause *case_command_add_clause(CaseCommand *cmd) {
    if (cmd->num_clauses >= cmd->clause_capacity) {
        cmd->clause_capacity *= 2;
        cmd->clauses = xrealloc(cmd->clauses,
                                sizeof(CaseClause) * (size_t)cmd->clause_capacity);
    }
    CaseClause *clause = &cmd->clauses[cmd->num_clauses++];
    clause->pattern_capacity = INITIAL_CAPACITY;
    clause->patterns = xmalloc(sizeof(char *) * (size_t)clause->pattern_capacity);
    clause->num_patterns = 0;
    clause->body_src = NULL;
    return clause;
}

void case_clause_add_pattern(CaseClause *clause, const char *pattern) {
    if (clause->num_patterns >= clause->pattern_capacity) {
        clause->pattern_capacity *= 2;
        clause->patterns = xrealloc(clause->patterns,
                                    sizeof(char *) * (size_t)clause->pattern_capacity);
    }
    clause->patterns[clause->num_patterns++] = xstrdup(pattern);
}

void case_command_free(CaseCommand *cmd) {
    if (!cmd) {
        return;
    }
    free(cmd->word);
    for (int i = 0; i < cmd->num_clauses; i++) {
        CaseClause *c = &cmd->clauses[i];
        for (int j = 0; j < c->num_patterns; j++) {
            free(c->patterns[j]);
        }
        free(c->patterns);
        free(c->body_src);
    }
    free(cmd->clauses);
    free(cmd);
}

WhileCommand *while_command_new(int is_until) {
    WhileCommand *cmd = xmalloc(sizeof(WhileCommand));
    cmd->cond_src = NULL;
    cmd->body_src = NULL;
    cmd->is_until = is_until;
    return cmd;
}

void while_command_free(WhileCommand *cmd) {
    if (!cmd) {
        return;
    }
    free(cmd->cond_src);
    free(cmd->body_src);
    free(cmd);
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

FunctionDef *function_def_new(const char *name) {
    FunctionDef *def = xmalloc(sizeof(FunctionDef));
    def->name = xstrdup(name);
    def->body_src = NULL;
    return def;
}

void function_def_free(FunctionDef *def) {
    if (!def) {
        return;
    }
    free(def->name);
    free(def->body_src);
    free(def);
}
