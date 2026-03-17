#include "pipeline.h"

#include <stdlib.h>
#include <unistd.h>

#include "table.h"
#include "util.h"


PipelineStage *pipeline_stage_new(
    Value *(*next)(PipelineStage *self),
    void (*free_fn)(PipelineStage *self),
    void *state,
    PipelineStage *upstream) {
    PipelineStage *stage = xmalloc(sizeof(PipelineStage));
    stage->next = next;
    stage->free_fn = free_fn;
    stage->state = state;
    stage->upstream = upstream;
    return stage;
}


void pipeline_stage_drain(PipelineStage *stage, FILE *out) {
    if (!stage || !out) {
        pipeline_stage_free(stage);
        return;
    }

    Value *v;
    while ((v = stage->next(stage)) != NULL) {
        if (v->type == VALUE_TABLE) {
            table_print(v->table, out);
        } else {
            char *s = value_to_string(v);
            fputs(s, out);
            fputc('\n', out);
            free(s);
        }
        value_free(v);
    }

    pipeline_stage_free(stage);
}


void pipeline_stage_drain_to_fd(PipelineStage *stage, int fd) {
    if (!stage || fd < 0) {
        pipeline_stage_free(stage);
        if (fd >= 0) {
            close(fd);
        }
        return;
    }

    FILE *out = fdopen(fd, "w");
    if (!out) {
        // fdopen failed — drain and discard, then close fd
        pipeline_stage_free(stage);
        close(fd);
        return;
    }

    // Reuse drain logic via FILE*
    Value *v;
    while ((v = stage->next(stage)) != NULL) {
        if (v->type == VALUE_TABLE) {
            table_print(v->table, out);
        } else {
            char *s = value_to_string(v);
            fputs(s, out);
            fputc('\n', out);
            free(s);
        }
        value_free(v);
    }

    fclose(out); // Also closes the underlying fd
    pipeline_stage_free(stage);
}


void pipeline_stage_free(PipelineStage *stage) {
    if (!stage) {
        return;
    }
    // Free upstream chain first
    pipeline_stage_free(stage->upstream);
    // Free stage-specific state
    if (stage->free_fn) {
        stage->free_fn(stage);
    }
    free(stage);
}
