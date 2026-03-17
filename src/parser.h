#ifndef SPLASH_PARSER_H
#define SPLASH_PARSER_H

#include "command.h"
#include "tokenizer.h"

// Parse a token list into a Pipeline AST.
// Returns NULL on empty input (no error), or NULL with error message on
// parse error. Caller takes ownership of the returned Pipeline.
Pipeline *parser_parse(const TokenList *tokens);

#endif // SPLASH_PARSER_H
