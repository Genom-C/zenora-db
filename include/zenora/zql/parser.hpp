#ifndef ZENORA_ZQL_PARSER_HPP
#define ZENORA_ZQL_PARSER_HPP

#include "zenora/zql/lexer.hpp"

#include <string>
#include <vector>

namespace zenora::zql
{

enum class StatementKind
{
    DatabaseCreate,
    DatabaseOpen,
    DatabaseClose,
    DatabaseInfo,
    DatabaseCheck,
    DatabaseCheckpoint,
    DatabaseBackup
};

struct Statement
{
    StatementKind kind;
    std::string argument;
    std::size_t line;
    std::size_t column;
};

struct ParseError
{
    std::string message;
    std::size_t line;
    std::size_t column;
};

struct ParseResult
{
    std::vector<Statement> statements;
    ParseError error;

    bool success() const noexcept;
};

class Parser
{
public:
    static ParseResult parse(const std::vector<Token> &tokens);
};

}

#endif
