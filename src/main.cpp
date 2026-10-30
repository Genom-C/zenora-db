#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

#include "zenora/core.h"
#include "zenora/zql/executor.hpp"
#include "zenora/zql/lexer.hpp"
#include "zenora/zql/parser.hpp"

namespace
{

constexpr std::size_t MAX_LINE_SIZE = 1024U * 1024U;

bool execute_line(
    const std::string &source,
    std::size_t line_number,
    zenora::zql::Executor &executor)
{
    using namespace zenora::zql;

    LexResult lexical = Lexer::tokenize(source);

    if (!lexical.success())
    {
        std::cerr << "Lexical error at line " << line_number
                  << ", column " << lexical.error.column
                  << ": " << lexical.error.message << '\n';
        return false;
    }

    ParseResult parsed = Parser::parse(lexical.tokens);

    if (!parsed.success())
    {
        std::cerr << "Syntax error at line " << line_number
                  << ", column " << parsed.error.column
                  << ": " << parsed.error.message << '\n';
        return false;
    }

    for (const Statement &statement : parsed.statements)
    {
        ExecutionResult result = executor.execute(statement);

        if (!result.success)
        {
            std::cerr << "Execution error: " << result.message << '\n';
            return false;
        }

        std::cout << result.message << '\n';
    }

    return true;
}

int run_script(
    const std::string &path,
    zenora::zql::Executor &executor)
{
    std::ifstream file(path);

    if (!file.is_open())
    {
        std::cerr << "Cannot open ZQL script: " << path << '\n';
        return 1;
    }

    std::string line;
    std::size_t line_number = 0;

    while (std::getline(file, line))
    {
        ++line_number;

        if (line.size() > MAX_LINE_SIZE)
        {
            std::cerr << "Line " << line_number
                      << " exceeds the maximum size\n";
            return 1;
        }

        if (!execute_line(line, line_number, executor))
            return 1;
    }

    if (file.bad())
    {
        std::cerr << "Error while reading ZQL script\n";
        return 1;
    }

    return 0;
}

std::string normalize_command(const std::string &line)
{
    std::string result;
    std::size_t first = line.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
        return result;

    std::size_t last = line.find_last_not_of(" \t\r\n");
    result = line.substr(first, last - first + 1);

    for (char &character : result)
    {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }

    return result;
}

int run_interactive(zenora::zql::Executor &executor)
{
    std::string line;

    std::cout << "ZENORA ZQL Interactive Shell\n";
    std::cout << "Type EXIT or QUIT to close the shell.\n";

    while (true)
    {
        std::cout << "zql> " << std::flush;

        if (!std::getline(std::cin, line))
            break;

        if (line.size() > MAX_LINE_SIZE)
        {
            std::cerr << "Input line exceeds the maximum size\n";
            continue;
        }

        std::string command = normalize_command(line);

        if (command == "EXIT" || command == "QUIT")
            break;

        execute_line(line, 1, executor);
    }

    return 0;
}

}

int main(int argc, char **argv)
{
    std::cout << "============================\n";
    std::cout << "       ZENORA DB\n";
    std::cout << "============================\n";
    std::cout << "Version: " << ZENORA_VERSION << '\n';

    if (zenora_core_init() != 0)
    {
        std::cerr << "Core initialization failed\n";
        return 1;
    }

    if (argc > 2)
    {
        std::cerr << "Usage: zenora [script.zql]\n";
        return 1;
    }

    zenora::zql::DatabaseSession session;
    zenora::zql::Executor executor(session);

    if (argc == 2)
        return run_script(argv[1], executor);

    return run_interactive(executor);
}
