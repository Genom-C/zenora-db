#ifndef ZENORA_ZQL_EXECUTOR_HPP
#define ZENORA_ZQL_EXECUTOR_HPP

#include "zenora/zql/database_session.hpp"
#include "zenora/zql/parser.hpp"

#include <string>

namespace zenora::zql
{

struct ExecutionResult
{
    bool success;
    std::string message;
};

class Executor
{
public:
    explicit Executor(DatabaseSession &session);

    ExecutionResult execute(const Statement &statement);

private:
    DatabaseSession &session_;
};

}

#endif
