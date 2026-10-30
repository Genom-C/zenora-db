#ifndef ZENORA_ZQL_LEXER_HPP
#define ZENORA_ZQL_LEXER_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace zenora::zql
{

enum class TokenKind
{
    End,
    Identifier,
    String,
    Integer,
    Decimal,
    Semicolon,
    Comma,
    LeftParen,
    RightParen,
    Equal,
    Colon,
    Star
};

struct Token
{
    TokenKind kind;
    std::string lexeme;
    std::string value;
    std::size_t line;
    std::size_t column;
};

struct LexError
{
    std::string message;
    std::size_t line;
    std::size_t column;
};

struct LexResult
{
    std::vector<Token> tokens;
    LexError error;

    bool success() const noexcept;
};

class Lexer
{
public:
    static LexResult tokenize(std::string_view source);
};

}

#endif
