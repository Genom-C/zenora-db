#include "zenora/zql/parser.hpp"

#include <cctype>
#include <utility>

namespace zenora::zql
{

bool ParseResult::success() const noexcept
{
    return error.message.empty();
}

namespace
{

class SyntaxParser
{
public:
    explicit SyntaxParser(const std::vector<Token> &tokens)
        : tokens_(tokens), position_(0)
    {
    }

    ParseResult run()
    {
        ParseResult result;

        if (tokens_.empty() || tokens_.back().kind != TokenKind::End)
        {
            result.error = {
                "Token stream is missing end marker",
                1,
                1
            };
            return result;
        }

        for (std::size_t i = 0; i + 1 < tokens_.size(); ++i)
        {
            if (tokens_[i].kind == TokenKind::End)
            {
                result.error = {
                    "Unexpected end marker before end of token stream",
                    tokens_[i].line,
                    tokens_[i].column
                };
                return result;
            }
        }

        while (!check(TokenKind::End))
        {
            Statement statement{};

            if (!parse_statement(statement, result.error))
            {
                result.statements.clear();
                return result;
            }

            result.statements.push_back(std::move(statement));
        }

        return result;
    }

private:
    const std::vector<Token> &tokens_;
    std::size_t position_;

    const Token &peek() const
    {
        return tokens_[position_];
    }

    bool check(TokenKind kind) const
    {
        return peek().kind == kind;
    }

    const Token &advance()
    {
        const Token &token = peek();

        if (!check(TokenKind::End))
            ++position_;

        return token;
    }

    static std::string uppercase(const std::string &value)
    {
        std::string result = value;

        for (char &character : result)
        {
            character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(character)));
        }

        return result;
    }

    bool match_keyword(const char *keyword)
    {
        if (!check(TokenKind::Identifier))
            return false;

        if (uppercase(peek().value) != keyword)
            return false;

        advance();
        return true;
    }

    bool expect_keyword(const char *keyword, ParseError &error)
    {
        if (match_keyword(keyword))
            return true;

        set_error(error, std::string("Expected keyword ") + keyword);
        return false;
    }

    bool expect_token(
        TokenKind kind,
        const char *description,
        ParseError &error)
    {
        if (check(kind))
        {
            advance();
            return true;
        }

        set_error(error, std::string("Expected ") + description);
        return false;
    }

    void set_error(ParseError &error, std::string message) const
    {
        error.message = std::move(message);
        error.line = peek().line;
        error.column = peek().column;
    }

    bool parse_statement(Statement &statement, ParseError &error)
    {
        const Token start = peek();

        if (!expect_keyword("DATABASE", error))
            return false;

        if (match_keyword("CREATE"))
        {
            statement.kind = StatementKind::DatabaseCreate;
            return parse_path_statement(statement, start, error);
        }

        if (match_keyword("OPEN"))
        {
            statement.kind = StatementKind::DatabaseOpen;
            return parse_path_statement(statement, start, error);
        }

        if (match_keyword("BACKUP"))
        {
            statement.kind = StatementKind::DatabaseBackup;
            return parse_path_statement(statement, start, error);
        }

        if (match_keyword("CLOSE"))
        {
            statement.kind = StatementKind::DatabaseClose;
        }
        else if (match_keyword("INFO"))
        {
            statement.kind = StatementKind::DatabaseInfo;
        }
        else if (match_keyword("CHECK"))
        {
            statement.kind = StatementKind::DatabaseCheck;
        }
        else if (match_keyword("CHECKPOINT"))
        {
            statement.kind = StatementKind::DatabaseCheckpoint;
        }
        else
        {
            set_error(error, "Unknown DATABASE operation");
            return false;
        }

        statement.line = start.line;
        statement.column = start.column;

        return expect_token(TokenKind::Semicolon, "semicolon", error);
    }

    bool parse_path_statement(
        Statement &statement,
        const Token &start,
        ParseError &error)
    {
        if (!expect_token(TokenKind::String, "quoted database path", error))
            return false;

        statement.argument = tokens_[position_ - 1].value;
        statement.line = start.line;
        statement.column = start.column;

        return expect_token(TokenKind::Semicolon, "semicolon", error);
    }
};

}

ParseResult Parser::parse(const std::vector<Token> &tokens)
{
    return SyntaxParser(tokens).run();
}

}
