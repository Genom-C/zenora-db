#include "zenora/zql/database_session.hpp"

#include "zenora/storage.h"

#include <sstream>
#include <utility>

namespace zenora::zql
{

DatabaseSession::DatabaseSession()
    : handle_(nullptr)
{
}

DatabaseSession::~DatabaseSession()
{
    if (handle_ != nullptr)
    {
        zenora_storage_handle_close(handle_);
        handle_ = nullptr;
    }
}

SessionResult DatabaseSession::create(const std::string &path)
{
    if (handle_ != nullptr)
        return {false, "A database is already open"};

    if (path.empty())
        return {false, "Database path cannot be empty"};

    int result = zenora_storage_create(path.c_str());

    if (result != 0)
        return {false, "Database creation failed with code " + std::to_string(result)};

    ZenoraStorageHandle *handle = nullptr;

    result = zenora_storage_handle_open(path.c_str(), 0, &handle);

    if (result != 0)
        return {false, "Database was created but could not be opened"};

    handle_ = handle;
    path_ = path;

    return {true, "Database created and opened: " + path_};
}

SessionResult DatabaseSession::open(const std::string &path)
{
    if (handle_ != nullptr)
        return {false, "A database is already open"};

    if (path.empty())
        return {false, "Database path cannot be empty"};

    ZenoraStorageHandle *handle = nullptr;

    int result = zenora_storage_handle_open(path.c_str(), 0, &handle);

    if (result != 0)
        return {false, "Database open failed with code " + std::to_string(result)};

    handle_ = handle;
    path_ = path;

    return {true, "Database opened: " + path_};
}

SessionResult DatabaseSession::close()
{
    if (handle_ == nullptr)
        return {false, "No database is open"};

    ZenoraStorageHandle *handle = handle_;
    handle_ = nullptr;
    path_.clear();

    int result = zenora_storage_handle_close(handle);

    if (result != 0)
        return {false, "Database close failed"};

    return {true, "Database closed"};
}

SessionResult DatabaseSession::info() const
{
    if (handle_ == nullptr)
        return {false, "No database is open"};

    uint32_t format_version = 0;
    uint64_t page_count = 0;

    if (zenora_storage_handle_get_format(handle_, &format_version) != 0)
        return {false, "Could not read database format"};

    if (zenora_storage_handle_get_page_count(handle_, &page_count) != 0)
        return {false, "Could not read database page count"};

    std::ostringstream output;

    output << "Database: " << path_
           << "\nFormat: " << format_version
           << "\nPage size: " << ZENORA_PAGE_SIZE
           << "\nPhysical page size: "
           << (format_version == ZENORA_STORAGE_FORMAT_V2
                   ? ZENORA_PHYSICAL_PAGE_SIZE
                   : ZENORA_PAGE_SIZE)
           << "\nPages: " << page_count
           << "\nAccess: READ ONLY";

    return {true, output.str()};
}

bool DatabaseSession::is_open() const noexcept
{
    return handle_ != nullptr;
}

const std::string &DatabaseSession::path() const noexcept
{
    return path_;
}

}
