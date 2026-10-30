cmake_minimum_required(VERSION 3.20)

project(ZENORA_DB
    VERSION 1.0.0
    LANGUAGES C CXX
)

enable_testing()

add_compile_definitions(ZENORA_VERSION="1.0.0")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(zenora_core STATIC
    src/core/core.c
    src/storage/storage.c
    src/storage/storage_handle.c
    src/storage/page_codec.c
    src/storage/page_manager.c
    src/storage/buffer_pool.c
    src/storage/wal.c
    src/storage/wal_v6.c
    src/storage/wal_page_codec.c
)

target_include_directories(zenora_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

add_executable(zenora
    src/main.cpp
)

target_link_libraries(zenora PRIVATE
    zenora_core
)

add_executable(wal_test
    tests/wal_test.c
)

target_link_libraries(wal_test PRIVATE
    zenora_core
)

add_executable(wal_integrity_test
    tests/wal_integrity_test.c
)

target_link_libraries(wal_integrity_test PRIVATE
    zenora_core
)


add_executable(wal_failstop_test
    tests/wal_failstop_test.c
)

target_link_libraries(wal_failstop_test PRIVATE
    zenora_core
)

add_executable(wal_chain_test
    tests/wal_chain_test.c
)

target_link_libraries(wal_chain_test PRIVATE
    zenora_core
)

add_executable(wal_attack_test
    tests/wal_attack_test.c
)

target_link_libraries(wal_attack_test PRIVATE
    zenora_core
)

add_executable(wal_payload_attack_test
    tests/wal_payload_attack_test.c
)

target_link_libraries(wal_payload_attack_test PRIVATE
    zenora_core
)

add_executable(wal_crash_test
    tests/wal_crash_test.c
)

target_link_libraries(wal_crash_test PRIVATE
    zenora_core
)

add_executable(wal_transaction_rules_test
    tests/wal_transaction_rules_test.c
)

target_link_libraries(wal_transaction_rules_test PRIVATE
    zenora_core
)

add_executable(wal_abort_test
    tests/wal_abort_test.c
)

target_link_libraries(wal_abort_test PRIVATE
    zenora_core
)

add_executable(wal_page_id_test
    tests/wal_page_id_test.c
)

target_link_libraries(wal_page_id_test PRIVATE
    zenora_core
)

add_executable(wal_v2_compat_test
    tests/wal_v2_compat_test.c
)

target_link_libraries(wal_v2_compat_test PRIVATE
    zenora_core
)

add_executable(wal_v4_test
    tests/wal_v4_test.c
)

target_link_libraries(wal_v4_test PRIVATE
    zenora_core
)

add_executable(wal_v5_test
    tests/wal_v5_test.c
)

target_link_libraries(wal_v5_test PRIVATE
    zenora_core
)

add_executable(page_codec_test
    tests/page_codec_test.c
)

target_link_libraries(page_codec_test PRIVATE
    zenora_core
)

add_executable(storage_format_test
    tests/storage_format_test.c
)

target_link_libraries(storage_format_test PRIVATE
    zenora_core
)

add_executable(buffer_pool_test
    tests/buffer_pool_test.c
)

target_link_libraries(buffer_pool_test PRIVATE
    zenora_core
)

add_executable(storage_benchmark
    tests/storage_benchmark.c
)

target_link_libraries(storage_benchmark PRIVATE
    zenora_core
)

add_executable(storage_handle_test
    tests/storage_handle_test.c
)

target_link_libraries(storage_handle_test PRIVATE
    zenora_core
)

add_executable(page_manager_session_test
    tests/page_manager_session_test.c
)

target_link_libraries(page_manager_session_test PRIVATE
    zenora_core
)

function(zenora_register_test test_name)
    add_test(
        NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
endfunction()

zenora_register_test(wal_test)
zenora_register_test(wal_integrity_test)
zenora_register_test(wal_failstop_test)
zenora_register_test(wal_chain_test)
zenora_register_test(wal_attack_test)
zenora_register_test(wal_payload_attack_test)
zenora_register_test(wal_crash_test)
zenora_register_test(wal_transaction_rules_test)
zenora_register_test(wal_abort_test)
zenora_register_test(wal_page_id_test)
zenora_register_test(wal_v2_compat_test)
zenora_register_test(wal_v4_test)
zenora_register_test(wal_v5_test)
zenora_register_test(page_codec_test)
zenora_register_test(storage_format_test)
zenora_register_test(buffer_pool_test)
zenora_register_test(storage_handle_test)
zenora_register_test(page_manager_session_test)

add_executable(wal_page_codec_test
    tests/wal_page_codec_test.c
)

target_link_libraries(wal_page_codec_test PRIVATE
    zenora_core
)

zenora_register_test(wal_page_codec_test)

add_executable(wal_v6_test
    tests/wal_v6_test.c
)

target_link_libraries(wal_v6_test PRIVATE
    zenora_core
)

zenora_register_test(wal_v6_test)

add_executable(wal_v6_attack_test
    tests/wal_v6_attack_test.c
)

target_link_libraries(wal_v6_attack_test PRIVATE
    zenora_core
)

zenora_register_test(wal_v6_attack_test)

add_executable(wal_v6_undo_chain_test
    tests/wal_v6_undo_chain_test.c
)

target_link_libraries(wal_v6_undo_chain_test PRIVATE
    zenora_core
)

zenora_register_test(wal_v6_undo_chain_test)


target_sources(zenora_core PRIVATE
    src/language/lexer.cpp
)

add_executable(zql_lexer_test
    tests/zql_lexer_test.cpp
)

target_link_libraries(zql_lexer_test PRIVATE
    zenora_core
)

zenora_register_test(zql_lexer_test)

target_sources(zenora_core PRIVATE
    src/language/parser.cpp
)

add_executable(zql_parser_test
    tests/zql_parser_test.cpp
)

target_link_libraries(zql_parser_test PRIVATE
    zenora_core
)

zenora_register_test(zql_parser_test)

target_sources(zenora_core PRIVATE
    src/language/database_session.cpp
    src/language/executor.cpp
)

add_executable(zql_executor_test
    tests/zql_executor_test.cpp
)

target_link_libraries(zql_executor_test PRIVATE
    zenora_core
)

zenora_register_test(zql_executor_test)
