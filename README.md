# SecureBank — Core Transaction Engine

A banking transaction engine built in **C++17** using embedded **SQLite**. The goal was to combine two subjects — **OOP** and **DBMS** — into one real project instead of keeping them separate.

---

## Tech Stack

- **Language:** C++17
- **Database:** SQLite (bundled amalgamation, no external install needed)
- **Build:** CMake 3.10+
- **CI:** GitHub Actions (build + tests on every push)

---

## What's Implemented

### OOP Concepts
- Abstract base class `Account` with pure virtual methods
- `SavingsAccount` and `CurrentAccount` override rules (minimum balance, transfer limit, interest)
- `BankEngine` only talks to `Account*` — runtime polymorphism, no type-checking `if` chains
- **Factory Pattern** — `AccountFactory` builds the right subclass from a DB string
- **Singleton** — `DatabaseManager` holds one SQLite connection, thread-safe lazy init
- **RAII** — `Statement` auto-finalizes prepared statements; `SqlTransaction` auto-rollbacks on any exception

### DBMS Concepts
- Schema is in **3NF** — no redundant data across tables
- `ACID` transfers: `BEGIN IMMEDIATE` + mutex so debit, credit, and audit row are always atomic
- `CHECK` constraints enforce non-negative balances and valid enums at the DB level
- `FOREIGN KEY` with `PRAGMA foreign_keys = ON` (SQLite has this off by default)
- `PRAGMA journal_mode = WAL` so readers don't block the writer
- Parameterized queries everywhere — no string-concatenated SQL, so no SQL injection possible
- All money stored as **integer paise** (not `double`) to avoid floating-point rounding errors

### APIs
| Method | What it does |
|--------|-------------|
| `transferFunds(src, dst, amount)` | ACID fund transfer between two accounts |
| `deposit(accountId, amount)` | Cash deposit into an account |
| `withdraw(accountId, amount)` | Cash withdrawal, respects minimum balance |
| `createUser(firstName, lastName, email)` | Register a new customer |
| `createAccount(userId, type, balance)` | Open a Savings or Current account |
| `postMonthlyInterest()` | Credits 4% p.a./12 to all Savings accounts |
| `getTransactionHistory(accountId)` | Per-account statement as a typed vector |

---

## Database Schema

```
Users
  user_id  PK | first_name | last_name | email UNIQUE
     |
     | 1──N (ON DELETE CASCADE)
     |
Accounts
  account_id PK | user_id FK | account_type CHECK('Savings','Current') | balance_paise CHECK(>=0)
     |
     | 1──N (no cascade — financial history must never disappear)
     |
Transactions
  transaction_id PK | source_account_id FK NULL | destination_account_id FK NULL
  amount_paise CHECK(>0) | transaction_type CHECK('TRANSFER','DEPOSIT','WITHDRAWAL','INTEREST')
  status CHECK('SUCCESS','FAILED') | failure_reason | timestamp
```

**Why `transaction_type` column?** — Deposits have no source, withdrawals have no destination, interest postings have no source. A single type column makes the audit log self-explanatory without needing separate tables.

---

## Account Rules

|  | Savings | Current |
|--|---------|---------|
| Minimum balance | Rs. 500 | None |
| Per-transfer limit | Rs. 50,000 | Rs. 5,00,000 |
| Monthly interest | 4% p.a. / 12 | None |

A transfer or withdrawal is rejected (and logged as `FAILED`) if:
- Amount ≤ 0, or source == destination
- Account does not exist
- Amount exceeds the per-transfer limit
- Balance would drop below the minimum

---

## Build & Run

SQLite is bundled — no external dependencies.

**With CMake:**
```bash
cmake -S . -B build
cmake --build build
./build/securebank          # demo
ctest --test-dir build      # run all 32 tests
```

**Without CMake (MinGW on Windows, drop `-ldl`):**
```bash
gcc -c sqlite3.c -o sqlite3.o
g++ -std=c++17 main.cpp  sqlite3.o -o securebank       -ldl -lpthread
g++ -std=c++17 tests.cpp sqlite3.o -o securebank_tests -ldl -lpthread
./securebank
./securebank_tests
```

---

## Project Structure

```
securebank.hpp    — entire engine (RAII, Singleton, OOP hierarchy, Factory, BankEngine)
main.cpp          — demo driver
tests.cpp         — 32 unit tests (no external framework)
CMakeLists.txt    — build config + ctest
sqlite3.c / .h    — SQLite amalgamation (public domain)
.github/workflows/ci.yml — CI pipeline
```

---

## Tests (32 total)

Covers: successful transfers, all rejection cases, atomicity via trigger-based crash simulation, DB `CHECK` constraint enforcement, FK protection, Factory validation, polymorphic rule checks, integer money math, 300-iteration money conservation, 4-thread concurrent transfer safety, deposit/withdraw APIs, `createUser`/`createAccount` input validation, duplicate email rejection, monthly interest math, and per-account transaction history.

---

## Limitations

- Single SQLite connection (no connection pool)
- No REST API or UI — engine + demo only
- No authentication or role-based access
- Interest is calculated per account; no batch reconciliation step

---

## Possible Extensions

REST API layer · Role-based access · Idempotency keys · Scheduled interest job · Currency support

