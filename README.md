# SecureBank Core Transaction Engine

A C++17 banking transaction engine backed by an embedded SQLite3 database. Built to practice and demonstrate object-oriented design patterns, RAII resource management, and ACID transaction handling.

## Features

- **Object-Oriented Domain Model:**
  - Abstract `Account` base class with `SavingsAccount` and `CurrentAccount` implementations.
  - Polymorphic withdrawal validation (`canWithdraw`) and interest calculation (`calculateInterest`):
    - `SavingsAccount`: enforces a Rs. 1,000 minimum balance requirement and 4.5% annual interest.
    - `CurrentAccount`: zero minimum balance with a Rs. 1,00,000 per-transaction limit.
- **Design Patterns & C++ Idioms:**
  - **Factory Pattern (`AccountFactory`):** Instantiates `SavingsAccount` or `CurrentAccount` objects from database records.
  - **Singleton Pattern (`DatabaseManager`):** Manages the SQLite connection with `std::mutex` synchronization and WAL (`Write-Ahead Logging`) mode enabled.
  - **RAII Wrappers (`ScopedStatement`, `TransactionGuard`):** Automatically finalizes prepared statements (`sqlite3_stmt`) and rolls back uncommitted transactions if an error or exception occurs.
- **Database Integrity & ACID Compliance:**
  - Normalized 3NF schema (`Users`, `Accounts`, `Transactions`) with foreign key constraints, `CHECK` constraints, and B-Tree indexes.
  - Uses parameterized prepared statements (`sqlite3_prepare_v2` / `sqlite3_bind_*`) for all queries.
  - Uses `BEGIN IMMEDIATE TRANSACTION` to prevent lock upgrade deadlocks (`SQLITE_BUSY`) during concurrent transfers.

## Tech Stack

- **Language:** C++17
- **Database:** SQLite3
- **Build:** Make / `g++`

## Project Structure

```text
SecureBank-Core-Engine/
├── main.cpp          # Domain models, RAII wrappers, transaction engine, and test driver
├── sqlite3.c         # SQLite3 amalgamation source
├── sqlite3.h         # SQLite3 header
├── Makefile          # Build configuration
└── README.md
```

## Build & Run

Using Make:
```bash
make
./secure_bank
```

Or compile directly with `g++`:
```bash
gcc -O2 -c sqlite3.c -o sqlite3.o
g++ -std=c++17 -Wall -Wextra -O2 -pthread main.cpp sqlite3.o -lpthread -ldl -o secure_bank
./secure_bank
```
