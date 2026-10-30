#ifndef ZENORA_ZQL_DATABASE_SESSION_HPP
#define ZENORA_ZQL_DATABASE_SESSION_HPP

#include "zenora/storage_handle.h"

#include <cstdint>
#include <string>

namespace zenora::zql
{

struct SessionResult
{
    bool success;
    std::string message;
};

class DatabaseSession
{
public:
    DatabaseSession();
    ~DatabaseSession();

    DatabaseSession(const DatabaseSession &) = delete;
    DatabaseSession &operator=(const DatabaseSession &) = delete;

    SessionResult create(const std::string &path);
    SessionResult open(const std::string &path);
    SessionResult close();
    SessionResult info() const;

    bool is_open() const noexcept;
    const std::string &path() const noexcept;

private:
    ZenoraStorageHandle *handle_;
    std::string path_;
};

}

#endif
