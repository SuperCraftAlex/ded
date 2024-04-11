#include <assert.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "common.h"
#include "lexer.h"

typedef struct {
    Token_Kind kind;
    const char *text;
} Literal_Token;

const char *token_kind_name[TOKEN_KIND_SIZE] = {
    [TOKEN_END] = "end of content",
    [TOKEN_INVALID] = "invalid token",
    [TOKEN_PREPROC] = "preprocessor directive",
    [TOKEN_SYMBOL] = "symbol",
    [TOKEN_OPEN_PAREN] = "open paren",
    [TOKEN_CLOSE_PAREN] = "close paren",
    [TOKEN_OPEN_CURLY] = "open curly",
    [TOKEN_CLOSE_CURLY] = "close curly",
    [TOKEN_SEMICOLON] = "semicolon",
    [TOKEN_KEYWORD] = "keyword",
    [TOKEN_COMMENT] = "comment",
    [TOKEN_STRING] = "string",
};

Lexer lexer_new(Free_Glyph_Atlas *atlas, const char *content, size_t content_len, String_Builder file_path)
{
    Lexer l = {0};
    l.atlas = atlas;
    l.content = content;
    l.content_len = content_len;
    l.file_ext = FEXT_CPP;
    if (file_path.items != NULL) {
        l.file_path.items = (char*) malloc(sizeof(char*) * (strlen(file_path.items) + 1));
        strcpy(l.file_path.items, file_path.items);

        const char *filename = l.file_path.items;
        const char *dot = strrchr(filename, '.');
        if (dot && dot != filename) {
            const char *file_ext_str = dot + 1;

            if (strcmp(file_ext_str, "kt") == 0 || strcmp(file_ext_str, "kts") == 0) {
                l.file_ext = FEXT_KOTLIN;
            } else if (strcmp(file_ext_str, "py") == 0) {
                l.file_ext = FEXT_PYTHON;
            } else if (strcmp(file_ext_str, "java") == 0) {
                l.file_ext = FEXT_JAVA;
            } else if (strcmp(file_ext_str, "miniconf") == 0) {
                l.file_ext = FEXT_MINICONF;
            }
        }
    }

    return l;
}

const char *file_ext_str(File_Extension ext)
{
    switch (ext) {
        case FEXT_KOTLIN:
            return "Kotlin";
        case FEXT_JAVA:
            return "Java";
        case FEXT_CPP:
            return "C++";
        case FEXT_PYTHON:
            return "Python";
        case FEXT_MINICONF:
            return "MiniConf";
        default:
            return "?";
    }
}

bool lexer_starts_with(Lexer *l, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) {
        return true;
    }
    if (l->cursor + prefix_len - 1 >= l->content_len) {
        return false;
    }
    for (size_t i = 0; i < prefix_len; ++i) {
        if (prefix[i] != l->content[l->cursor + i]) {
            return false;
        }
    }
    return true;
}

void lexer_chop_char(Lexer *l, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        // TODO: get rid of this assert by checking the length of the choped prefix upfront
        assert(l->cursor < l->content_len);
        char x = l->content[l->cursor];
        l->cursor += 1;
        if (x == '\n') {
            l->line += 1;
            l->bol = l->cursor;
            l->x = 0;
        } else {
            if (l->atlas) {
                size_t glyph_index = x;
                // TODO: support for glyphs outside of ASCII range
                if (glyph_index >= GLYPH_METRICS_CAPACITY) {
                    glyph_index = '?';
                }
                Glyph_Metric metric = l->atlas->metrics[glyph_index];
                l->x += metric.ax;
            }
        }
    }
}

void lexer_trim_left(Lexer *l)
{
    while (l->cursor < l->content_len && isspace(l->content[l->cursor])) {
        lexer_chop_char(l, 1);
    }
}

bool is_symbol_start(char x)
{
    return isalpha(x) || x == '_';
}

bool is_symbol(char x)
{
    return isalnum(x) || x == '_';
}

Token lexer_next(Lexer *l)
{
    lexer_trim_left(l);

    Token token = {
        .text = &l->content[l->cursor],
    };

    token.position.x = l->x;
    token.position.y = -(float)l->line * FREE_GLYPH_FONT_SIZE;

    if (l->cursor >= l->content_len) return token;

    if (l->content[l->cursor] == '"') {
        // TODO: TOKEN_STRING should also handle escape sequences
        token.kind = TOKEN_STRING;
        lexer_chop_char(l, 1);
        while (l->cursor < l->content_len && l->content[l->cursor] != '"' && l->content[l->cursor] != '\n') {
            lexer_chop_char(l, 1);
        }
        if (l->cursor < l->content_len) {
            lexer_chop_char(l, 1);
        }
        token.text_len = &l->content[l->cursor] - token.text;
        return token;
    }

    if (l->content[l->cursor] == '#') {
        // TODO: preproc should also handle newlines
        token.kind = TOKEN_PREPROC;
        while (l->cursor < l->content_len && l->content[l->cursor] != '\n') {
            lexer_chop_char(l, 1);
        }
        if (l->cursor < l->content_len) {
            lexer_chop_char(l, 1);
        }
        token.text_len = &l->content[l->cursor] - token.text;
        return token;
    }

    if (lexer_starts_with(l, "//")) {
        token.kind = TOKEN_COMMENT;
        while (l->cursor < l->content_len && l->content[l->cursor] != '\n') {
            lexer_chop_char(l, 1);
        }
        if (l->cursor < l->content_len) {
            lexer_chop_char(l, 1);
        }
        token.text_len = &l->content[l->cursor] - token.text;
        return token;
    }
    
    for (size_t i = 0; i < literal_tokens_count; ++i) {
        if (lexer_starts_with(l, literal_tokens[i].text)) {
            // NOTE: this code assumes that there is no newlines in literal_tokens[i].text
            size_t text_len = strlen(literal_tokens[i].text);
            token.kind = literal_tokens[i].kind;
            token.text_len = text_len;
            lexer_chop_char(l, text_len);
            return token;
        }
    }

    if (is_symbol_start(l->content[l->cursor])) {
        token.kind = TOKEN_SYMBOL;
        while (l->cursor < l->content_len && is_symbol(l->content[l->cursor])) {
            lexer_chop_char(l, 1);
            token.text_len += 1;
        }
        
        const char **keywords;
        size_t keywords_count;
        switch (l->file_ext) {
            case FEXT_JAVA:
                keywords = jKeywords;
                keywords_count = jKeywords_count;
            break;

            case FEXT_KOTLIN:
                keywords = ktKeywords;
                keywords_count = ktKeywords_count;
            break;

            case FEXT_PYTHON:
                keywords = pyKeywords;
                keywords_count = pyKeywords_count;
            break;

            case FEXT_MINICONF:
                keywords = miniconfKeywords;
                keywords_count = miniconfKeywords_count;
            break;

            default:
                keywords = cKeywords;
                keywords_count = cKeywords_count;
        }
        

        for (size_t i = 0; i < keywords_count; ++i) {
            size_t keyword_len = strlen(keywords[i]);
            if (keyword_len == token.text_len && memcmp(keywords[i], token.text, keyword_len) == 0) {
                token.kind = TOKEN_KEYWORD;
                break;
            }
        }
        
        return token;
    }

    lexer_chop_char(l, 1);
    token.kind = TOKEN_INVALID;
    token.text_len = 1;
    return token;
}
