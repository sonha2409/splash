#ifndef SPLASH_TOKENIZER_H
#define SPLASH_TOKENIZER_H

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,              // |
    TOKEN_PIPE_STRUCTURED,   // |>
    TOKEN_REDIRECT_IN,       // <
    TOKEN_REDIRECT_OUT,      // >
    TOKEN_REDIRECT_APPEND,   // >>
    TOKEN_REDIRECT_ERR,      // 2>
    TOKEN_REDIRECT_OUT_ERR,  // >&
    TOKEN_REDIRECT_APPEND_ERR, // >>&
    TOKEN_BACKGROUND,        // &
    TOKEN_SEMICOLON,         // ;
    TOKEN_DSEMI,             // ;;
    TOKEN_AND,               // &&
    TOKEN_OR,                // ||
    TOKEN_LPAREN,            // (
    TOKEN_RPAREN,            // )
    TOKEN_NEWLINE,           // \n
    TOKEN_DOLLAR_PAREN,      // $(
    TOKEN_BACKTICK,          // `
    TOKEN_PROCESS_SUB_IN,    // <(
    TOKEN_PROCESS_SUB_OUT,   // >(
    TOKEN_EOF,
    TOKEN_INCOMPLETE,        // Unterminated quote or escape
} TokenType;

typedef struct {
    TokenType type;
    char *value;    // Literal text of the token (owned, heap-allocated)
    int pos;        // Start position in input string
    int length;     // Length in input string
} Token;

typedef struct {
    Token *tokens;  // Dynamic array (owned)
    int count;
    int capacity;
} TokenList;

// Tokenize an input string. Caller takes ownership of the returned TokenList
// and must call token_list_free() when done.
TokenList *tokenizer_tokenize(const char *input);

// Free a TokenList and all its tokens.
void token_list_free(TokenList *list);

// Return a human-readable name for a token type.
const char *token_type_name(TokenType type);

#endif // SPLASH_TOKENIZER_H
