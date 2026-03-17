#ifndef SPLASH_PIPELINE_H
#define SPLASH_PIPELINE_H

#include <stdio.h>

#include "value.h"

// A structured pipeline stage — produces Values one at a time via pull-based
// lazy evaluation. Sources generate values; filters transform upstream values.
typedef struct PipelineStage {
    // Pull the next value. Returns an owned Value*, or NULL when exhausted.
    Value *(*next)(struct PipelineStage *self);
    // Destructor — frees stage-specific state (not upstream).
    void (*free_fn)(struct PipelineStage *self);
    // Opaque per-stage data (owned by stage).
    void *state;
    // Upstream stage, or NULL for sources. Owned by this stage.
    struct PipelineStage *upstream;
} PipelineStage;

// Creates a new pipeline stage. Caller takes ownership.
// upstream may be NULL (for source stages). Ownership of upstream is transferred.
PipelineStage *pipeline_stage_new(
    Value *(*next)(PipelineStage *self),
    void (*free_fn)(PipelineStage *self),
    void *state,
    PipelineStage *upstream);

// Pull all values from the final stage and print them.
// Tables are pretty-printed; other values use value_to_string().
// Consumes the stage (calls pipeline_stage_free when done).
void pipeline_stage_drain(PipelineStage *stage, FILE *out);

// Pull all values from the stage and write them to a raw file descriptor.
// Tables are pretty-printed; other values use value_to_string() + newline.
// Consumes the stage (calls pipeline_stage_free when done).
// Closes fd when done writing.
void pipeline_stage_drain_to_fd(PipelineStage *stage, int fd);

// Free a stage and its entire upstream chain recursively.
void pipeline_stage_free(PipelineStage *stage);

#endif // SPLASH_PIPELINE_H
