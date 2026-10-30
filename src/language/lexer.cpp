#include "zenora/zql/lexer.hpp"

#include <cctype>
#include <utility>

namespace zenora::zql
{

bool LexResult::success() const noexcept
{
    return error.message.empty();
}

namespace
{

class Scanner
{
public:
    explicit Scanner(std::string_view source)
        : source_(source), position_(0), line_(1), column_(1)
    {
    }

    LexResult run()
    {
        LexResult result;

        while (!at_end())
        {
            if (is_whitespace(peek()))
            {
                advance();
                continue;
            }

            const std::size_t start = position_;
            const std::size_t start_line = line_;
            const std::size_t start_column = column_;

            const unsigned char current =
                static_cast<unsigned char>(peek());

            if (is_identifier_start(current))
            {
                scan_identifier(result, start, start_line, start_column);
                continue;
            }

            if (is_digit(current))
            {
                scan_number(result, start, start_line, start_column);
                continue;
            }

            if (peek() == '"')
            {
                if (!scan_string(result, start, start_line, start_column))
                {
                    return result;
                }

                continue;
            }

            if (scan_punctuation(result, start, start_line, start_column))
            {
                continue;
            }

            result.error = {
                "Unexpected character",
                start_line,
                start_column
            };

            return result;
        }

        result.tokens.push_back({
            TokenKind::End,
            "",
            "",
            line_,
            column_
        });

        return result;
    }

private:
    std::string_view source_;
    std::size_t position_;
    std::size_t line_;
    std::size_t column_;

    bool at_end() const
    {
        return position_ >= source_.size();
    }

    char peek() const
    {
        return at_end() ? '\0' : source_[position_];
    }

    char advance()
    {
        const char current = source_[position_++];

        if (current == '\n')
        {
            ++line_;
            column_ = 1;
        }
        else
        {
            ++column_;
        }

        return current;
    }

    bool match(char expected)
    {
        if (at_end() || peek() != expected)
        {
            return false;
        }

        advance();
        return true;
    }

    static bool is_whitespace(char value)
    {
        return value == ' ' ||
               value == '\t' ||
               value == '\r' ||
               value == '\n' ||
               value == '\f' ||
               value == '\v';
    }

    static bool is_digit(unsigned char value)
    {
        return value >= '0' && value <= '9';
    }

    static bool is_alpha(unsigned char value)
    {
        return (value >= 'a' && value <= 'z') ||
               (value >= 'A' && value <= 'Z');
    }

    static bool is_identifier_start(unsigned char value)
    {
        return is_alpha(value) || value == '_';
    }

    static bool is_identifier_part(unsigned char value)
    {
        return is_identifier_start(value) || is_digit(value);
    }

    void add_token(
        LexResult &result,
        TokenKind kind,
        std::size_t start,
        std::size_t start_line,
        std::size_t start_column,
        std::string value = "")
    {
        const std::string lexeme(
            source_.substr(start, position_ - start));

        if (value.empty() && kind != TokenKind::String)
        {
            value = lexeme;
        }

        result.tokens.push_back({
            kind,
            lexeme,
            std::move(value),
            start_line,
            start_column
        });
    }

    void scan_identifier(
        LexResult &result,
        std::size_t start,
        std::size_t start_line,
        std::size_t start_column)
    {
        while (!at_end() &&
               is_identifier_part(
                   static_cast<unsigned char>(peek())))
        {
            advance();
        }

        add_token(
            result,
            TokenKind::Identifier,
            start,
            start_line,
            start_column);
    }

    void scan_number(
        LexResult &result,
        std::size_t start,
        std::size_t start_line,
        std::size_t start_column)
    {
        while (!at_end() &&
               is_digit(static_cast<unsigned char>(peek())))
        {
            advance();
        }

        TokenKind kind = TokenKind::Integer;

        if (!at_end() &&
            peek() == '.' &&
            position_ + 1 < source_.size() &&
            is_digit(static_cast<unsigned char>(source_[position_ + 1])))
        {
            advance();

            while (!at_end() &&
                   is_digit(static_cast<unsigned char>(peek())))
            {
                advance();
            }

            kind = TokenKind::Decimal;
        }

        add_token(
            result,
            kind,
            start,
            start_line,
            start_column);
    }

    bool scan_string(
        LexResult &result,
        std::size_t start,
        std::size_t start_line,
        std::size_t start_column)
    {
        advance();

        std::string value;

        while (!at_end())
        {
            const char current = advance();

            if (current == '"')
            {
                add_token(
                    result,
                    TokenKind::String,
                    start,
                    start_line,
                    start_column,
                    std::move(value));

                return true;
            }

            if (current == '\n' || current == '\r')
            {
                result.error = {
                    "Unescaped newline in string literal",
                    start_line,
                    start_column
                };

                return false;
            }

            if (current != '\\')
            {
                value.push_back(current);
                continue;
            }

            if (at_end())
            {
                result.error = {
                    "Incomplete escape sequence",
                    line_,
                    column_
                };

                return false;
            }

            const char escaped = advance();

            switch (escaped)
            {
                case '"':
                    value.push_back('"');
                    break;

                case '\\':
                    value.push_back('\\');
                    break;

                case 'n':
                    value.push_back('\n');
                    break;

                case 'r':
                    value.push_back('\r');
                    break;

                case 't':
                    value.push_back('\t');
                    break;

                default:
                    result.error = {
                        "Unsupported escape sequence",
                        line_,
                        column_ - 2
                    };

                    return false;
            }
        }

        result.error = {
            "Unterminated string literal",
            start_line,
            start_column
        };

        return false;
    }

    bool scan_punctuation(
        LexResult &result,
        std::size_t start,
        std::size_t start_line,
        std::size_t start_column)
    {
        TokenKind kind;

        switch (advance())
        {
            case ';':
                kind = TokenKind::Semicolon;
                break;

            case ',':
                kind = TokenKind::Comma;
                break;

            case '(':
                kind = TokenKind::LeftParen;
                break;

            case ')':
                kind = TokenKind::RightParen;
                break;

            case '=':
                kind = TokenKind::Equal;
                break;

            case ':':
                kind = TokenKind::Colon;
                break;

            case '*':
                kind = TokenKind::Star;
                break;

            default:
                --position_;
                --column_;
                return false;
        }

        add_token(
            result,
            kind,
            start,
            start_line,
            start_column);

        return true;
    }
};

}

LexResult Lexer::tokenize(std::string_view source)
{
    return Scanner(source).run();
}

}
