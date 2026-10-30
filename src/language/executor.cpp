#include "zenora/zql/executor.hpp"

namespace zenora::zql
{

Executor::Executor(DatabaseSession &session)
    : session_(session)
{
}

ExecutionResult Executor::execute(const Statement &statement)
{
    SessionResult result{false, "Unsupported database command"};

    switch (statement.kind)
    {
        case StatementKind::DatabaseCreate:
            result = session_.create(statement.argument);
            break;

        case StatementKind::DatabaseOpen:
            result = session_.open(statement.argument);
            break;

        case StatementKind::DatabaseClose:
            result = session_.close();
            break;

        case StatementKind::DatabaseInfo:
            result = session_.info();
            break;

        case StatementKind::DatabaseCheck:
            result = {false, "DATABASE CHECK is not implemented"};
            break;

        case StatementKind::DatabaseCheckpoint:
            result = {false, "DATABASE CHECKPOINT is not implemented"};
            break;

        case StatementKind::DatabaseBackup:
            result = {false, "DATABASE BACKUP is not implemented"};
            break;
    }

    return {result.success, result.message};
}

}
