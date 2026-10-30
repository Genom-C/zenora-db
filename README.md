# ZENORA DB

A database project I started building because I wanted to understand how database engines actually work under the hood.

ZENORA stands for Zero-latency Engine for Native Organized Record Access.

The name sounds ambitious, but this project is still unfinished. It is not a production-ready database. It is a personal engineering project where I explored storage engines, page management, write-ahead logging, transactions and a custom query language.

I built it using C and C++. I also used AI as a development assistant for research, debugging, architecture discussions and some implementation work.

This repository is basically where I left the project.

## Why I started this

I wanted to build something more interesting than a simple file manager or a basic key-value store.

The main idea was to create a database engine focused on files and metadata, with support for managing large amounts of data without loading everything into memory at once.

I also wanted to understand how database internals work instead of relying on an existing database library to handle everything.

## Project status

**Status: Unfinished**

**Current version: 1.0.0**

The project reached a point where several important components were implemented and tested, but some major features are still missing.

The version number does not mean the engine is feature complete or production ready.

## Built with

| Technology | Purpose |
| --- | --- |
| C | Core storage engine and low-level operations |
| C++17 | Query language and higher-level components |
| CMake | Build system |

## Architecture

ZENORA uses a layered architecture.

The low-level storage components are written in C, while the query language and higher-level execution components are written in C++.

    flowchart TD
        A["ZQL Source"] --> B["Lexer"]
        B --> C["Parser and AST"]
        C --> D["Validator"]
        D --> E["Executor"]
        E --> F["C Storage API"]
        F --> G["Buffer Pool"]
        G --> H["Page Manager"]
        H --> I["Storage Handle"]
        I --> J["Database File"]
        E --> K["WAL"]
        K --> L["Write Ahead Log File"]

The goal is to keep the query language separate from the storage engine so both sides can evolve independently.

## Storage engine

The storage engine is one of the main parts of this project.

Instead of treating the database as a simple collection of files, ZENORA uses a page-based storage approach.

Implemented components include:

- Storage format version 2
- Physical page encoding and decoding
- CRC32C checksums
- Page metadata
- Page identifiers
- Page allocation
- Buffer pool caching
- Page eviction
- Dirty page tracking
- Persistent storage handles

The physical page format separates page metadata from the actual payload.

## Buffer pool

The buffer pool keeps frequently used database pages in memory.

It currently supports:

- Page fetching
- Page pinning and unpinning
- Cache hit and miss tracking
- Page eviction
- Dirty page tracking
- Flushing pages to storage

The goal is to reduce unnecessary disk operations and make page access more efficient.

## Write ahead logging

ZENORA includes a version 6 WAL implementation.

The WAL is designed around transaction records and log sequence numbers.

Implemented parts include:

- Transaction state tracking
- Commit and abort records
- Log sequence numbers
- Durable LSN tracking
- Page update records
- Compensation log records
- Undo chain validation
- Record checksums
- WAL scanning and reopening
- Validation against malformed or corrupted records

The design follows the write-ahead logging principle.

Database pages should not be written to persistent storage before their corresponding log records are durable.

However, having a WAL implementation does not automatically mean the database can recover from every crash scenario.

## ZQL

ZQL stands for ZENORA Query Language.

I wanted a native language for the project instead of building another SQL clone.

The planned language covers databases, files, metadata and transactions.

Example syntax:

    DATABASE CREATE "example.zdb";
    DATABASE OPEN "example.zdb";
    DATABASE INFO;
    FILE LIST "/documents";
    META GET "/documents/report.pdf";
    TRANSACTION BEGIN;
    TRANSACTION COMMIT;

These examples show the intended language style. Not every command shown here is fully implemented.

The language pipeline currently includes:

- Lexer
- Parser
- Abstract syntax tree
- Command validation
- Executor
- Database session management

The original plan was to support both an interactive command line and execution of .zql files.

## File and metadata database

One of the main ideas behind ZENORA was to treat files and their metadata as database records.

The intended storage model separates file metadata, file indexing and file content storage.

Files are intended to be stored internally inside the database instead of relying on their original external paths.

For large files, the design is intended to support chunk-based storage so that the entire file does not need to be loaded into memory at once.

This part of the project was still under development when I stopped.

## Testing

Testing was an important part of development.

The project contains tests for storage formats, page codecs, buffer pool behavior, storage handles, WAL validation and ZQL execution.

At the last recorded test run, CTest reported:

**25 tests passed and 0 tests failed**

This result only describes the tests that existed at that point. It does not mean every feature is complete or that all possible failure scenarios have been covered.

## What is still missing

There is still a lot of work left.

Some of the major unfinished areas are:

- Complete crash recovery
- Full REDO and UNDO processing
- Safe recovery of page allocation operations
- Complete WAL and buffer pool integration testing
- Transaction isolation
- Concurrency control
- Multiple writer support
- Complete file import and export operations
- Full implementation of the ZQL command set
- More extensive stress testing
- Performance benchmarking under larger workloads

Crash recovery is one of the biggest missing pieces.

Without a complete and thoroughly tested recovery system, the engine should not be trusted with important production data.

## A little help from AI

I did use AI during development.

It helped with researching database concepts, discussing architecture, reviewing code, investigating bugs and working through some implementation details.

But this was not a one-prompt project where an entire database appeared automatically.

The project involved many iterations, design decisions, code changes, builds and tests.

AI was a tool in the development process, not a replacement for understanding the system.

## Final thoughts

ZENORA DB is an unfinished project, but I learned a lot while building it.

Working on storage pages, buffer pools, WAL records and transaction handling gave me a much better understanding of what happens inside a database engine.

I stopped development before reaching the original goal.

Maybe I will come back to it someday. For now, I am keeping this repository as a record of what I built, what I learned and what still needs to be done.

Thanks for checking it out.
