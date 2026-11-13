# SecureBank Core Transaction Engine

![CI](../../actions/workflows/ci.yml/badge.svg)

A core-banking transaction engine written in **C++17** on top of embedded **SQLite**.
It is a learning project built to apply two core CS subjects in one place:

* **OOP** – abstraction, inheritance, runtime polymorphism, Factory and Singleton patterns, RAII
* **DBMS** – ACID transactions, normalised schema, integrity constraints, prepared statements

> This is a portfolio / learning project, not production banking software
> (see [Limitations](#limitations)).

---

## Features

| Area | What is implemented |
|------|---------------------|
| **OOP** | Abstract `Account` base class; `SavingsAccount` and `CurrentAccount` override pure virtual rules (minimum balance, per-transfer limit, monthly interest). The engine talks only to `Account*` – runtime polymorphism. |
| **Factory** | `AccountFactory` builds the correct subclass from the `account_type` string stored in the DB. |
| **Singleton** | `DatabaseManager` owns the single SQLite connection (thread-safe lazy initialisation). |
| **RAII** | `Statement` (auto-finalizes prepared statements) and `SqlTransaction` (auto-`ROLLBACK` unless `commit()` is reached). |
| **ACID transfers** | `transferFunds` runs debit + credit + audit row inside one `BEGIN IMMEDIATE … COMMIT` block. Any failure rolls everything back. |
| **Deposit / Withdraw** | `deposit` and `withdraw` are first-class operations. Each respects per-transfer limits and minimum-balance rules, and is logged with its own `transaction_type`. |
| **Account management** | `createUser` and `createAccount` let you build new customers and accounts at runtime (with input validation and DB-enforced uniqueness). |
| **Interest posting** | `postMonthlyInterest` actually credits 4 % p.a. / 12 to every Savings account in a single atomic transaction and logs each posting as `INTEREST` type. |
| **Transaction history** | `getTransactionHistory(accountId)` returns a typed `TransactionRecord` vector for per-account statement generation. |
| **Integrity** | `CHECK (balance_paise >= 0)`, `CHECK (amount_paise > 0)`, `UNIQUE(email)`, foreign keys (enforced via `PRAGMA foreign_keys = ON`), account-type `CHECK`, transaction-type `CHECK`. |
| **Money safety** | All amounts are stored as **integer paise** – no floating-point rounding errors. |
| **Security** | Every SQL value is passed through bound parameters (`sqlite3_bind_*`) – no string-built SQL, so no SQL injection. |
| **Audit trail** | Every attempted operation (transfer, deposit, withdrawal, interest) is stored in `Transactions` with its `transaction_type`, `status` (`SUCCESS` / `FAILED`), and a `failure_reason`. |
| **Concurrency** | Whole transactions are serialised by a mutex (one shared connection) and by SQLite's write lock (`BEGIN IMMEDIATE`). |
| **Tests** | 32 unit tests covering all APIs, run under ASan / UBSan / TSan during development. |

## Database schema (3NF)

```
Users(user_id PK, first_name, last_name, email UNIQUE)
   │ 1
   │ ── N  (ON DELETE CASCADE)
Accounts(account_id PK, user_id FK, account_type CHECK IN ('Savings','Current'),
         balance_paise CHECK >= 0)
   │ 1
   │ ── N  (no cascade: an account with history can't be deleted)
Transactions(transaction_id PK, source_account_id FK NULL, destination_account_id FK NULL,
             amount_paise CHECK > 0,
             transaction_type CHECK IN ('TRANSFER','DEPOSIT','WITHDRAWAL','INTEREST'),
             status CHECK IN ('SUCCESS','FAILED'),
             failure_reason, timestamp)
```

Each fact is stored once and every non-key column depends only on its table's key (no partial or
transitive dependencies). Customer data is not repeated in `Accounts` or `Transactions`.

### transaction_type semantics

| type | source | destination |
|------|--------|-------------|
| `TRANSFER` | sending account | receiving account |
| `DEPOSIT` | NULL (cash enters system) | credited account |
| `WITHDRAWAL` | debited account | NULL (cash leaves system) |
| `INTEREST` | NULL (interest earned) | credited Savings account |

## Transfer rules

A transfer / withdrawal is rejected (balances unchanged) when:

1. the amount is `<= 0`, or source and destination are the same account
2. the source or destination account does not exist
3. the amount exceeds the account type's per-transfer limit
4. the source would drop below its minimum balance

| | Savings | Current |
|---|---|---|
| Minimum balance | Rs. 500 | none |
| Per-transfer limit | Rs. 50,000 | Rs. 5,00,000 |
| Interest | 4 % p.a. | none |

Rejections caused by business rules (3, 4, and missing accounts) are logged as `FAILED` in the audit trail.

## Build & run

Requires a C++17 compiler (g++ / clang++ / MSVC). SQLite is bundled (`sqlite3.c`, `sqlite3.h`).

**With CMake**
```bash
cmake -S . -B build
cmake --build build
./build/securebank            # demo
ctest --test-dir build        # unit tests
```

**Without CMake** (g++/MinGW) – note that `sqlite3.c` is C, so compile it with `gcc`, not `g++`:
```bash
gcc -c sqlite3.c -o sqlite3.o
g++ -std=c++17 main.cpp  sqlite3.o -o securebank       -ldl -lpthread
g++ -std=c++17 tests.cpp sqlite3.o -o securebank_tests -ldl -lpthread
./securebank
./securebank_tests
```
(On Windows/MinGW drop `-ldl`.)

## Sample output (abridged)

```
--- Cash deposits ---
  [OK]       Deposit Rs. 10000.00 -> account #3
             -> Deposited Rs. 10000.00 to account #3.
  [REJECTED] Deposit Rs.     0.00 -> account #1 (invalid amount)
             -> Deposit amount must be greater than zero.

--- Fund transfers ---
  [OK]       1 -> 2 : Rs. 1500.00
             -> Transferred Rs. 1500.00 from account #1 to account #2.
  [REJECTED] 1 -> 2 : Rs. 3100.00 (would break Rs. 500 minimum)
             -> Insufficient funds (a minimum balance of Rs. 500.00 must remain in the account).

--- Cash withdrawals ---
  [OK]       Withdraw Rs. 5000.00 from account #2 (Current, no minimum)
             -> Withdrew Rs. 5000.00 from account #2.
  [REJECTED] Withdraw Rs. 9600.00 from account #3 (Savings minimum enforced)
             -> Insufficient funds (a minimum balance of Rs. 500.00 must remain).

--- Monthly interest posting ---
  Total interest credited across all Savings accounts: Rs. 50.16

--- Transaction history for account #1 ---
  #2 | ? -> 1 | Rs. 1500.00 | TRANSFER | SUCCESS
  #4 | 1 -> 2 | Rs. 1500.00 | TRANSFER | SUCCESS
  ...
```

## Project structure

```
securebank.hpp   Engine: RAII wrappers, Singleton, Account hierarchy, Factory, BankEngine
main.cpp         Demo driver
tests.cpp        32 dependency-free unit tests
CMakeLists.txt   Build + ctest
sqlite3.c/.h     SQLite amalgamation (public domain)
```

## Design decisions

* **Integer paise instead of `double`** – binary floating point cannot represent values like 0.1 exactly.
* **`BEGIN IMMEDIATE`** – the transfer reads a balance and then updates it; taking the write lock first
  prevents another writer from changing the balance in between.
* **RAII transaction guard** – rollback happens automatically on every error path.
* **Business rules live in the subclasses** – the engine never checks "if Savings …"; adding a new
  account type means one new class and one line in the factory.
* **No `ON DELETE CASCADE` on `Transactions`** – financial history must never disappear silently.
* **`transaction_type` column** – the DB `CHECK` constraint enforces only valid types at the storage
  level, decoupling the application enum from raw SQL strings.
* **Cursor closed before writes in `postMonthlyInterest`** – all Savings account rows are fetched into
  a `std::vector` first, then the SELECT `Statement` is finalized, before any `UPDATE`/`INSERT` runs
  inside the same transaction. This avoids holding an open read cursor across write operations.

## Limitations

* Single process, single shared connection (not a connection pool).
* No authentication / authorisation, no REST or UI layer – it is an engine plus a demo.
* No currency conversion.
* Interest is calculated per account individually; there is no batch-rounding reconciliation step.

## Possible next steps

Deposit / withdraw ✅ | Interest posting ✅ | Account creation ✅ |
REST API layer, role-based access control, idempotency keys for transfers, scheduled interest job.
