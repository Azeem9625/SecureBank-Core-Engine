#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sqlite3.h"

using namespace std;

namespace securebank {

// Money is stored as integer paise; floating point is never used for balances.
using Money = int64_t;

inline Money rupees(double r) {
    return static_cast<Money>(llround(r * 100.0));
}

inline string formatMoney(Money paise) {
    ostringstream os;
    if (paise < 0) { os << '-'; paise = -paise; }
    os << "Rs. " << paise / 100
       << '.' << setfill('0') << setw(2) << paise % 100;
    return os.str();
}

// RAII wrapper: finalizes the prepared statement on scope exit.
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            throw runtime_error(string("SQL prepare failed: ") + sqlite3_errmsg(db));
    }

    ~Statement() { sqlite3_finalize(stmt_); }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, int64_t value) {
        sqlite3_bind_int64(stmt_, index, value);
        return *this;
    }
    Statement& bindText(int index, const string& value) {
        sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    Statement& bindNull(int index) {
        sqlite3_bind_null(stmt_, index);
        return *this;
    }

    bool step() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW)  return true;
        if (rc == SQLITE_DONE) return false;
        throw runtime_error(sqlite3_errmsg(db_));
    }

    int64_t columnInt(int i)  const { return sqlite3_column_int64(stmt_, i); }
    bool    columnIsNull(int i) const { return sqlite3_column_type(stmt_, i) == SQLITE_NULL; }
    string  columnText(int i) const {
        const unsigned char* t = sqlite3_column_text(stmt_, i);
        return t ? reinterpret_cast<const char*>(t) : "";
    }
    int rowsChanged() const { return sqlite3_changes(db_); }

private:
    sqlite3*      db_;
    sqlite3_stmt* stmt_ = nullptr;
};

// RAII transaction: rolls back unless commit() is called.
// BEGIN IMMEDIATE takes the write lock up front to avoid TOCTOU races.
class SqlTransaction {
public:
    explicit SqlTransaction(sqlite3* db) : db_(db) {
        run("BEGIN IMMEDIATE;");
    }

    ~SqlTransaction() {
        if (!committed_)
            sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }

    SqlTransaction(const SqlTransaction&) = delete;
    SqlTransaction& operator=(const SqlTransaction&) = delete;

    void commit() {
        run("COMMIT;");
        committed_ = true;
    }

private:
    void run(const char* sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw runtime_error(msg);
        }
    }

    sqlite3* db_;
    bool     committed_ = false;
};

// Singleton (Meyers): one SQLite connection for the program lifetime.
class DatabaseManager {
public:
    static void setDatabasePath(const string& path) {
        pathRef() = path;
    }

    static DatabaseManager& getInstance() {
        static DatabaseManager instance;
        return instance;
    }

    sqlite3* connection() const { return db_; }

    void execute(const string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw runtime_error(msg);
        }
    }

    void resetDemoData() {
        SqlTransaction tx(db_);
        execute("DELETE FROM Transactions;"
                "DELETE FROM Accounts;"
                "DELETE FROM Users;"
                "DELETE FROM sqlite_sequence;");
        execute(seedSql());
        tx.commit();
    }

    ~DatabaseManager() { if (db_) sqlite3_close(db_); }

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

private:
    DatabaseManager() {
        if (sqlite3_open(pathRef().c_str(), &db_) != SQLITE_OK) {
            string msg = sqlite3_errmsg(db_);
            sqlite3_close(db_); db_ = nullptr;
            throw runtime_error("Can't open database: " + msg);
        }
        try {
            sqlite3_busy_timeout(db_, 5000);
            execute("PRAGMA foreign_keys = ON;");
            execute("PRAGMA journal_mode = WAL;");
            createSchema();
        } catch (...) {
            sqlite3_close(db_); db_ = nullptr;
            throw;
        }
    }

    static string& pathRef() {
        static string path = "secure_bank.db";
        return path;
    }

    static string seedSql() {
        return
            "INSERT OR IGNORE INTO Users (user_id, first_name, last_name, email) "
            "VALUES (1, 'Azeem', 'Khan', 'azeem@dtu.ac.in');"

            "INSERT OR IGNORE INTO Users (user_id, first_name, last_name, email) "
            "VALUES (2, 'Raja', 'Kumar', 'raja@dtu.ac.in');"

            "INSERT OR IGNORE INTO Accounts (account_id, user_id, account_type, balance_paise) "
            "VALUES (1, 1, 'Savings', 500000);"

            "INSERT OR IGNORE INTO Accounts (account_id, user_id, account_type, balance_paise) "
            "VALUES (2, 2, 'Current', 1200000);";
    }

    void createSchema() {
        execute(
            "CREATE TABLE IF NOT EXISTS Users ("
            "  user_id    INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  first_name TEXT NOT NULL,"
            "  last_name  TEXT NOT NULL,"
            "  email      TEXT UNIQUE NOT NULL);"

            "CREATE TABLE IF NOT EXISTS Accounts ("
            "  account_id    INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  user_id       INTEGER NOT NULL,"
            "  account_type  TEXT    NOT NULL CHECK (account_type IN ('Savings','Current')),"
            "  balance_paise INTEGER NOT NULL CHECK (balance_paise >= 0),"
            "  FOREIGN KEY (user_id) REFERENCES Users(user_id) ON DELETE CASCADE);"

            "CREATE TABLE IF NOT EXISTS Transactions ("
            "  transaction_id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  source_account_id      INTEGER,"
            "  destination_account_id INTEGER,"
            "  amount_paise           INTEGER NOT NULL CHECK (amount_paise > 0),"
            "  transaction_type       TEXT    NOT NULL CHECK (transaction_type IN"
            "                             ('TRANSFER','DEPOSIT','WITHDRAWAL','INTEREST')),"
            "  status                 TEXT    NOT NULL CHECK (status IN ('SUCCESS','FAILED')),"
            "  failure_reason         TEXT,"
            "  timestamp              DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "  FOREIGN KEY (source_account_id)      REFERENCES Accounts(account_id),"
            "  FOREIGN KEY (destination_account_id) REFERENCES Accounts(account_id));"

            "CREATE INDEX IF NOT EXISTS idx_txn_source ON Transactions(source_account_id);"
            "CREATE INDEX IF NOT EXISTS idx_txn_dest   ON Transactions(destination_account_id);"
        );
        execute(seedSql());
    }

    sqlite3* db_ = nullptr;
};

// BankEngine depends only on Account*, so new account types need no engine changes.
class Account {
public:
    Account(int accountId, int userId, Money balance, string type)
        : accountId_(accountId), userId_(userId),
          balance_(balance), type_(move(type)) {}

    virtual ~Account() = default;

    virtual Money minimumBalance()      const = 0;
    virtual Money perTransferLimit()    const = 0;
    virtual Money monthlyInterest()     const = 0;
    virtual void  displayAccountDetails() const = 0;

    bool canDebit(Money amount) const {
        return balance_ - amount >= minimumBalance();
    }

    int          getAccountId() const { return accountId_; }
    int          getUserId()    const { return userId_;    }
    Money        getBalance()   const { return balance_;   }
    const string& getType()    const { return type_;      }

protected:
    void printCommon() const {
        cout << "[" << type_ << " Account]"
             << "  ID: "               << accountId_
             << "  |  User ID: "       << userId_
             << "  |  Balance: "       << formatMoney(balance_)
             << "  |  Min Balance: "   << formatMoney(minimumBalance())
             << "  |  Transfer Limit: "<< formatMoney(perTransferLimit())
             << "  |  Interest/month: "<< formatMoney(monthlyInterest())
             << "\n";
    }

    int    accountId_;
    int    userId_;
    Money  balance_;
    string type_;
};

class SavingsAccount : public Account {
public:
    SavingsAccount(int id, int userId, Money balance)
        : Account(id, userId, balance, "Savings") {}

    Money minimumBalance()   const override { return rupees(500);    }
    Money perTransferLimit() const override { return rupees(50000);  }
    Money monthlyInterest()  const override { return balance_ * 4 / 1200; }
    void  displayAccountDetails() const override { printCommon(); }
};

class CurrentAccount : public Account {
public:
    CurrentAccount(int id, int userId, Money balance)
        : Account(id, userId, balance, "Current") {}

    Money minimumBalance()   const override { return 0;              }
    Money perTransferLimit() const override { return rupees(500000); }
    Money monthlyInterest()  const override { return 0;              }
    void  displayAccountDetails() const override { printCommon(); }
};

class AccountFactory {
public:
    static unique_ptr<Account> createAccount(int id, int userId,
                                              Money balance,
                                              const string& type) {
        if (type == "Savings") return make_unique<SavingsAccount>(id, userId, balance);
        if (type == "Current") return make_unique<CurrentAccount>(id, userId, balance);
        throw invalid_argument("Invalid account type: " + type);
    }
};

enum class TransferStatus {
    Success,
    InvalidAmount,
    SameAccount,
    SourceNotFound,
    DestinationNotFound,
    InsufficientFunds,
    LimitExceeded,
    DatabaseError
};

enum class TransactionType { Transfer, Deposit, Withdrawal, Interest };

inline const char* toDbString(TransactionType t) {
    switch (t) {
        case TransactionType::Transfer:   return "TRANSFER";
        case TransactionType::Deposit:    return "DEPOSIT";
        case TransactionType::Withdrawal: return "WITHDRAWAL";
        case TransactionType::Interest:   return "INTEREST";
    }
    return "TRANSFER";
}

struct TransferResult {
    TransferStatus status;
    string         message;
    bool ok() const { return status == TransferStatus::Success; }
};

struct TransactionRecord {
    int64_t id;
    int     sourceId;    // -1 when SQL NULL
    int     destId;      // -1 when SQL NULL
    Money   amount;
    string  type;
    string  status;
    string  reason;
    string  timestamp;
};

// A single engine-wide mutex serialises every operation.
class BankEngine {
public:

    static TransferResult transferFunds(int sourceId, int destId, Money amount) {

        if (amount <= 0)
            return {TransferStatus::InvalidAmount, "Amount must be greater than zero."};
        if (sourceId == destId)
            return {TransferStatus::SameAccount, "Source and destination must be different."};

        lock_guard<mutex> lock(engineMutex());
        sqlite3* db = DatabaseManager::getInstance().connection();

        try {
            SqlTransaction tx(db);

            auto source = loadAccount(db, sourceId);
            auto dest   = loadAccount(db, destId);

            // Failed attempts are logged and committed so the audit trail is complete.
            auto reject = [&](TransferStatus s, const string& reason) {
                logTransaction(db,
                    source ? sourceId : 0,
                    dest   ? destId   : 0,
                    amount, TransactionType::Transfer, "FAILED", reason);
                tx.commit();
                return TransferResult{s, reason};
            };

            if (!source)
                return reject(TransferStatus::SourceNotFound, "Source account not found.");
            if (!dest)
                return reject(TransferStatus::DestinationNotFound, "Destination account not found.");
            if (amount > source->perTransferLimit())
                return reject(TransferStatus::LimitExceeded,
                    "Amount exceeds per-transfer limit of " +
                    formatMoney(source->perTransferLimit()) +
                    " for a " + source->getType() + " account.");
            if (!source->canDebit(amount))
                return reject(TransferStatus::InsufficientFunds,
                    "Insufficient funds (minimum balance of " +
                    formatMoney(source->minimumBalance()) + " must remain).");

            applyDelta(db, sourceId, -amount);
            applyDelta(db, destId,   +amount);
            logTransaction(db, sourceId, destId, amount,
                           TransactionType::Transfer, "SUCCESS", "");
            tx.commit();

            return {TransferStatus::Success,
                    "Transferred " + formatMoney(amount) +
                    " from account #" + to_string(sourceId) +
                    " to account #"   + to_string(destId) + "."};

        } catch (const exception& e) {
            return {TransferStatus::DatabaseError,
                    string("Database error, transaction rolled back: ") + e.what()};
        }
    }

    static TransferResult deposit(int accountId, Money amount) {

        if (amount <= 0)
            return {TransferStatus::InvalidAmount, "Deposit amount must be greater than zero."};

        lock_guard<mutex> lock(engineMutex());
        sqlite3* db = DatabaseManager::getInstance().connection();

        try {
            SqlTransaction tx(db);
            auto account = loadAccount(db, accountId);

            if (!account) {
                logTransaction(db, 0, 0, amount, TransactionType::Deposit,
                               "FAILED", "Account not found.");
                tx.commit();
                return {TransferStatus::DestinationNotFound, "Account not found."};
            }

            applyDelta(db, accountId, +amount);
            logTransaction(db, 0, accountId, amount,
                           TransactionType::Deposit, "SUCCESS", "");
            tx.commit();

            return {TransferStatus::Success,
                    "Deposited " + formatMoney(amount) +
                    " to account #" + to_string(accountId) + "."};

        } catch (const exception& e) {
            return {TransferStatus::DatabaseError,
                    string("Database error: ") + e.what()};
        }
    }

    static TransferResult withdraw(int accountId, Money amount) {

        if (amount <= 0)
            return {TransferStatus::InvalidAmount, "Withdrawal amount must be greater than zero."};

        lock_guard<mutex> lock(engineMutex());
        sqlite3* db = DatabaseManager::getInstance().connection();

        try {
            SqlTransaction tx(db);
            auto account = loadAccount(db, accountId);

            auto reject = [&](TransferStatus s, const string& reason) {
                logTransaction(db,
                    account ? accountId : 0, 0,
                    amount, TransactionType::Withdrawal, "FAILED", reason);
                tx.commit();
                return TransferResult{s, reason};
            };

            if (!account)
                return reject(TransferStatus::SourceNotFound, "Account not found.");
            if (amount > account->perTransferLimit())
                return reject(TransferStatus::LimitExceeded,
                    "Amount exceeds per-transfer limit of " +
                    formatMoney(account->perTransferLimit()) + ".");
            if (!account->canDebit(amount))
                return reject(TransferStatus::InsufficientFunds,
                    "Insufficient funds (minimum balance of " +
                    formatMoney(account->minimumBalance()) + " must remain).");

            applyDelta(db, accountId, -amount);
            logTransaction(db, accountId, 0, amount,
                           TransactionType::Withdrawal, "SUCCESS", "");
            tx.commit();

            return {TransferStatus::Success,
                    "Withdrew " + formatMoney(amount) +
                    " from account #" + to_string(accountId) + "."};

        } catch (const exception& e) {
            return {TransferStatus::DatabaseError,
                    string("Database error: ") + e.what()};
        }
    }

    static int createUser(const string& firstName,
                          const string& lastName,
                          const string& email) {
        if (firstName.empty() || lastName.empty())
            throw invalid_argument("First name and last name must not be empty.");
        if (email.find('@') == string::npos)
            throw invalid_argument("Invalid email address.");

        lock_guard<mutex> lock(engineMutex());
        sqlite3* db = DatabaseManager::getInstance().connection();

        SqlTransaction tx(db);
        Statement s(db, "INSERT INTO Users (first_name, last_name, email) VALUES (?, ?, ?);");
        s.bindText(1, firstName).bindText(2, lastName).bindText(3, email);
        s.step();

        int newId = static_cast<int>(sqlite3_last_insert_rowid(db));
        tx.commit();
        return newId;
    }

    static int createAccount(int userId, const string& type, Money initialBalance = 0) {
        if (type != "Savings" && type != "Current")
            throw invalid_argument("Account type must be 'Savings' or 'Current'.");
        if (initialBalance < 0)
            throw invalid_argument("Initial balance must be non-negative.");

        lock_guard<mutex> lock(engineMutex());
        sqlite3* db = DatabaseManager::getInstance().connection();

        SqlTransaction tx(db);
        Statement s(db, "INSERT INTO Accounts (user_id, account_type, balance_paise) VALUES (?, ?, ?);");
        s.bind(1, userId).bindText(2, type).bind(3, initialBalance);
        s.step();

        int newId = static_cast<int>(sqlite3_last_insert_rowid(db));
        tx.commit();
        return newId;
    }

    static Money postMonthlyInterest() {
        lock_guard<mutex> lock(engineMutex());
        sqlite3* db = DatabaseManager::getInstance().connection();

        struct Row { int id; int userId; Money balance; };
        vector<Row> savings;
        {
            Statement sel(db,
                "SELECT account_id, user_id, balance_paise "
                "FROM Accounts WHERE account_type = 'Savings';");
            while (sel.step())
                savings.push_back({
                    static_cast<int>(sel.columnInt(0)),
                    static_cast<int>(sel.columnInt(1)),
                    sel.columnInt(2)
                });
        }

        SqlTransaction tx(db);
        Money totalInterest = 0;

        for (const auto& row : savings) {
            SavingsAccount sa(row.id, row.userId, row.balance);
            Money interest = sa.monthlyInterest();
            if (interest > 0) {
                applyDelta(db, row.id, interest);
                logTransaction(db, 0, row.id, interest,
                               TransactionType::Interest, "SUCCESS", "");
                totalInterest += interest;
            }
        }

        tx.commit();
        return totalInterest;
    }

    static unique_ptr<Account> getAccount(int accountId) {
        lock_guard<mutex> lock(engineMutex());
        return loadAccount(DatabaseManager::getInstance().connection(), accountId);
    }

    static Money totalMoneyInSystem() {
        lock_guard<mutex> lock(engineMutex());
        Statement s(DatabaseManager::getInstance().connection(),
                    "SELECT COALESCE(SUM(balance_paise), 0) FROM Accounts;");
        s.step();
        return s.columnInt(0);
    }

    static vector<TransactionRecord> getTransactionHistory(int accountId) {
        lock_guard<mutex> lock(engineMutex());
        Statement s(DatabaseManager::getInstance().connection(),
            "SELECT transaction_id, source_account_id, destination_account_id,"
            "       amount_paise, transaction_type, status,"
            "       COALESCE(failure_reason,''), timestamp"
            "  FROM Transactions"
            " WHERE source_account_id = ? OR destination_account_id = ?"
            " ORDER BY transaction_id;");
        s.bind(1, accountId).bind(2, accountId);

        vector<TransactionRecord> records;
        while (s.step()) {
            TransactionRecord r;
            r.id       = s.columnInt(0);
            r.sourceId = s.columnIsNull(1) ? -1 : static_cast<int>(s.columnInt(1));
            r.destId   = s.columnIsNull(2) ? -1 : static_cast<int>(s.columnInt(2));
            r.amount   = s.columnInt(3);
            r.type     = s.columnText(4);
            r.status   = s.columnText(5);
            r.reason   = s.columnText(6);
            r.timestamp= s.columnText(7);
            records.push_back(r);
        }
        return records;
    }

    static void printAllAccounts() {
        lock_guard<mutex> lock(engineMutex());
        sqlite3* db = DatabaseManager::getInstance().connection();
        Statement s(db, "SELECT account_id FROM Accounts ORDER BY account_id;");
        while (s.step()) {
            auto acc = loadAccount(db, static_cast<int>(s.columnInt(0)));
            if (acc) acc->displayAccountDetails();
        }
    }

    static void printAuditLog() {
        lock_guard<mutex> lock(engineMutex());
        Statement s(DatabaseManager::getInstance().connection(),
            "SELECT transaction_id, source_account_id, destination_account_id,"
            "       amount_paise, transaction_type, status,"
            "       COALESCE(failure_reason,'')"
            "  FROM Transactions ORDER BY transaction_id;");

        while (s.step()) {
            string src = s.columnIsNull(1) ? "?" : to_string(s.columnInt(1));
            string dst = s.columnIsNull(2) ? "?" : to_string(s.columnInt(2));
            cout << "  #" << s.columnInt(0)
                 << " | " << src << " -> " << dst
                 << " | " << formatMoney(s.columnInt(3))
                 << " | " << s.columnText(4)
                 << " | " << s.columnText(5);
            if (!s.columnText(6).empty())
                cout << " (" << s.columnText(6) << ")";
            cout << "\n";
        }
    }

private:
    static mutex& engineMutex() {
        static mutex m;
        return m;
    }

    static unique_ptr<Account> loadAccount(sqlite3* db, int accountId) {
        Statement s(db,
            "SELECT user_id, balance_paise, account_type "
            "FROM Accounts WHERE account_id = ?;");
        s.bind(1, accountId);
        if (!s.step()) return nullptr;
        return AccountFactory::createAccount(
            accountId,
            static_cast<int>(s.columnInt(0)),
            s.columnInt(1),
            s.columnText(2));
    }

    static void applyDelta(sqlite3* db, int accountId, Money delta) {
        Statement s(db,
            "UPDATE Accounts SET balance_paise = balance_paise + ? "
            "WHERE account_id = ?;");
        s.bind(1, delta).bind(2, accountId);
        s.step();
        if (s.rowsChanged() != 1)
            throw runtime_error("Account update affected no row.");
    }

    static void logTransaction(sqlite3* db,
                                int sourceId, int destId,
                                Money amount,
                                TransactionType type,
                                const char* status,
                                const string& reason) {
        Statement s(db,
            "INSERT INTO Transactions"
            " (source_account_id, destination_account_id, amount_paise,"
            "  transaction_type, status, failure_reason)"
            " VALUES (?, ?, ?, ?, ?, ?);");

        if (sourceId) s.bind(1, sourceId); else s.bindNull(1);
        if (destId)   s.bind(2, destId);   else s.bindNull(2);

        s.bind(3, amount)
         .bindText(4, toDbString(type))
         .bindText(5, status);

        if (reason.empty()) s.bindNull(6);
        else                s.bindText(6, reason);

        s.step();
    }
};

}
