#ifndef SPLASH_PARSER_H
#define SPLASH_PARSER_H

#include "command.h"
#include "tokenizer.h"

// Parse a token list into a CommandList AST.
// input is the original source string (used to extract raw text for compound
// command bodies that need re-evaluation on each execution, e.g. for-loop bodies).
// Returns NULL on empty input (no error), or NULL with error message on
// parse error. Caller takes ownership of the returned CommandList.
CommandList *parser_parse(const TokenList *tokens, const char *input);

#endif // SPLASH_PARSER_H
