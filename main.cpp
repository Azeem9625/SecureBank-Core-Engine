#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <thread>
#include <cmath>
#include "sqlite3.h"

class ScopedStatement {
private:
    sqlite3_stmt* stmt = nullptr;

public:
    ScopedStatement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("Prepare failed: ") + sqlite3_errmsg(db));
        }
    }

    ~ScopedStatement() {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }

    ScopedStatement(const ScopedStatement&) = delete;
    ScopedStatement& operator=(const ScopedStatement&) = delete;

    sqlite3_stmt* get() const { return stmt; }
};

class TransactionGuard {
private:
    sqlite3* db;
    bool committed = false;

public:
    explicit TransactionGuard(sqlite3* database) : db(database) {
        char* errMsg = nullptr;
        // BEGIN IMMEDIATE locks for writing upfront to avoid SQLITE_BUSY under contention
        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::string err = errMsg ? errMsg : "Unknown error";
            if (errMsg) sqlite3_free(errMsg);
            throw std::runtime_error("Transaction start failed: " + err);
        }
    }

    void commit() {
        char* errMsg = nullptr;
        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::string err = errMsg ? errMsg : "Unknown error";
            if (errMsg) sqlite3_free(errMsg);
            throw std::runtime_error("Commit failed: " + err);
        }
        committed = true;
    }

    ~TransactionGuard() {
        if (!committed) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
};

class DatabaseManager {
private:
    sqlite3* db = nullptr;
    std::mutex dbMutex;

    DatabaseManager() {
        int rc = sqlite3_open("secure_bank.db", &db);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
            std::exit(1);
        }

        sqlite3_busy_timeout(db, 5000);
        execSQL("PRAGMA foreign_keys = ON;");
        execSQL("PRAGMA journal_mode = WAL;");
        execSQL("PRAGMA synchronous = NORMAL;");

        initSchema();
    }

    void execSQL(const char* sql) {
        char* errMsg = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "SQL error: " << (errMsg ? errMsg : "unknown") << std::endl;
            if (errMsg) sqlite3_free(errMsg);
        }
    }

    void initSchema() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS Users ("
            "user_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "first_name TEXT NOT NULL, "
            "last_name TEXT NOT NULL, "
            "email TEXT UNIQUE NOT NULL, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);"

            "CREATE TABLE IF NOT EXISTS Accounts ("
            "account_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "user_id INTEGER NOT NULL, "
            "account_type TEXT NOT NULL CHECK(account_type IN ('Savings', 'Current')), "
            "balance REAL NOT NULL CHECK(balance >= 0.0), "
            "FOREIGN KEY(user_id) REFERENCES Users(user_id) ON DELETE CASCADE);"

            "CREATE TABLE IF NOT EXISTS Transactions ("
            "transaction_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "source_account_id INTEGER, "
            "destination_account_id INTEGER, "
            "amount REAL NOT NULL CHECK(amount > 0.0), "
            "status TEXT NOT NULL CHECK(status IN ('SUCCESS', 'FAILED')), "
            "remarks TEXT NOT NULL, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
            "FOREIGN KEY(source_account_id) REFERENCES Accounts(account_id), "
            "FOREIGN KEY(destination_account_id) REFERENCES Accounts(account_id));"

            "CREATE INDEX IF NOT EXISTS idx_accounts_user ON Accounts(user_id);"
            "CREATE INDEX IF NOT EXISTS idx_tx_source ON Transactions(source_account_id);"
            "CREATE INDEX IF NOT EXISTS idx_tx_dest ON Transactions(destination_account_id);"

            "INSERT OR IGNORE INTO Users (user_id, first_name, last_name, email) VALUES (1, 'Azeem', 'Khan', 'azeem@dtu.ac.in');"
            "INSERT OR IGNORE INTO Users (user_id, first_name, last_name, email) VALUES (2, 'Raja', 'Kumar', 'raja@dtu.ac.in');"
            "INSERT OR IGNORE INTO Accounts (account_id, user_id, account_type, balance) VALUES (1, 1, 'Savings', 5000.00);"
            "INSERT OR IGNORE INTO Accounts (account_id, user_id, account_type, balance) VALUES (2, 2, 'Current', 12000.00);";

        execSQL(sql);
    }

public:
    static DatabaseManager& getInstance() {
        static DatabaseManager instance;
        return instance;
    }

    sqlite3* getConnection() { return db; }
    std::mutex& getMutex() { return dbMutex; }

    void resetBalances() {
        std::lock_guard<std::mutex> lock(dbMutex);
        execSQL("UPDATE Accounts SET balance = 5000.00 WHERE account_id = 1;");
        execSQL("UPDATE Accounts SET balance = 12000.00 WHERE account_id = 2;");
    }

    ~DatabaseManager() {
        if (db) sqlite3_close(db);
    }

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
};

class Account {
protected:
    int accountId;
    int userId;
    double balance;
    std::string accountType;

public:
    Account(int accId, int uId, double bal, std::string type)
        : accountId(accId), userId(uId), balance(bal), accountType(std::move(type)) {}

    virtual ~Account() = default;

    virtual void displayAccountDetails() const = 0;
    virtual bool canWithdraw(double amount, std::string& reason) const = 0;
    virtual double calculateInterest() const = 0;

    int getAccountId() const { return accountId; }
    int getUserId() const { return userId; }
    double getBalance() const { return balance; }
    const std::string& getAccountType() const { return accountType; }
};

class SavingsAccount : public Account {
private:
    static constexpr double MIN_BALANCE = 1000.00;
    static constexpr double INTEREST_RATE = 0.045;

public:
    SavingsAccount(int accId, int uId, double bal)
        : Account(accId, uId, bal, "Savings") {}

    void displayAccountDetails() const override {
        std::cout << "[Savings] ID: " << accountId
                  << " | User: " << userId
                  << " | Balance: Rs. " << std::fixed << std::setprecision(2) << balance
                  << " | Annual Interest: Rs. " << calculateInterest() << "\n";
    }

    bool canWithdraw(double amount, std::string& reason) const override {
        if (balance < amount) {
            reason = "Insufficient funds";
            return false;
        }
        if ((balance - amount) < MIN_BALANCE) {
            reason = "Minimum balance of Rs. 1000.00 required for Savings account";
            return false;
        }
        return true;
    }

    double calculateInterest() const override {
        return std::round((balance * INTEREST_RATE) * 100.0) / 100.0;
    }
};

class CurrentAccount : public Account {
private:
    static constexpr double TX_LIMIT = 100000.00;

public:
    CurrentAccount(int accId, int uId, double bal)
        : Account(accId, uId, bal, "Current") {}

    void displayAccountDetails() const override {
        std::cout << "[Current] ID: " << accountId
                  << " | User: " << userId
                  << " | Balance: Rs. " << std::fixed << std::setprecision(2) << balance << "\n";
    }

    bool canWithdraw(double amount, std::string& reason) const override {
        if (amount > TX_LIMIT) {
            reason = "Exceeds per-transaction limit for Current account";
            return false;
        }
        if (balance < amount) {
            reason = "Insufficient funds";
            return false;
        }
        return true;
    }

    double calculateInterest() const override {
        return 0.0;
    }
};

class AccountFactory {
public:
    static std::unique_ptr<Account> createAccount(int accId, int uId, double bal, const std::string& type) {
        if (type == "Savings") {
            return std::make_unique<SavingsAccount>(accId, uId, bal);
        } else if (type == "Current") {
            return std::make_unique<CurrentAccount>(accId, uId, bal);
        }
        throw std::invalid_argument("Unknown account type: " + type);
    }
};

class BankEngine {
private:
    static std::unique_ptr<Account> getAccount(sqlite3* db, int accountId) {
        ScopedStatement stmt(db, "SELECT account_id, user_id, balance, account_type FROM Accounts WHERE account_id = ?;");
        sqlite3_bind_int(stmt.get(), 1, accountId);

        if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            int accId = sqlite3_column_int(stmt.get(), 0);
            int uId = sqlite3_column_int(stmt.get(), 1);
            double bal = sqlite3_column_double(stmt.get(), 2);
            const unsigned char* rawType = sqlite3_column_text(stmt.get(), 3);
            std::string type = rawType ? reinterpret_cast<const char*>(rawType) : "";
            return AccountFactory::createAccount(accId, uId, bal, type);
        }
        return nullptr;
    }

    static void logTransaction(sqlite3* db, int sourceId, int destId, double amount,
                               const std::string& status, const std::string& remarks) {
        ScopedStatement stmt(db,
            "INSERT INTO Transactions (source_account_id, destination_account_id, amount, status, remarks) "
            "VALUES (?, ?, ?, ?, ?);");
        sqlite3_bind_int(stmt.get(), 1, sourceId);
        sqlite3_bind_int(stmt.get(), 2, destId);
        sqlite3_bind_double(stmt.get(), 3, amount);
        sqlite3_bind_text(stmt.get(), 4, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt.get(), 5, remarks.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt.get());
    }

public:
    static bool transferFunds(int sourceId, int destId, double amount, bool verbose = true) {
        amount = std::round(amount * 100.0) / 100.0;

        if (amount <= 0.0) {
            if (verbose) {
                std::cout << "[Error] Transfer amount must be positive.\n";
            }
            return false;
        }

        if (sourceId == destId) {
            if (verbose) {
                std::cout << "[Error] Source and destination accounts cannot be the same.\n";
            }
            return false;
        }

        auto& dbMgr = DatabaseManager::getInstance();
        std::lock_guard<std::mutex> lock(dbMgr.getMutex());
        sqlite3* db = dbMgr.getConnection();

        try {
            TransactionGuard tx(db);

            auto sourceAcc = getAccount(db, sourceId);
            auto destAcc = getAccount(db, destId);

            if (!sourceAcc || !destAcc) {
                if (verbose) {
                    std::cout << "[Rolled Back] Invalid source or destination account ID.\n";
                }
                return false;
            }

            std::string reason;
            if (!sourceAcc->canWithdraw(amount, reason)) {
                if (verbose) {
                    std::cout << "[Rolled Back] Account #" << sourceId << ": " << reason << "\n";
                }
                return false;
            }

            {
                ScopedStatement debit(db, "UPDATE Accounts SET balance = ROUND(balance - ?, 2) WHERE account_id = ?;");
                sqlite3_bind_double(debit.get(), 1, amount);
                sqlite3_bind_int(debit.get(), 2, sourceId);
                if (sqlite3_step(debit.get()) != SQLITE_DONE || sqlite3_changes(db) != 1) {
                    throw std::runtime_error("Debit failed");
                }
            }

            {
                ScopedStatement credit(db, "UPDATE Accounts SET balance = ROUND(balance + ?, 2) WHERE account_id = ?;");
                sqlite3_bind_double(credit.get(), 1, amount);
                sqlite3_bind_int(credit.get(), 2, destId);
                if (sqlite3_step(credit.get()) != SQLITE_DONE || sqlite3_changes(db) != 1) {
                    throw std::runtime_error("Credit failed");
                }
            }

            logTransaction(db, sourceId, destId, amount, "SUCCESS", "Fund Transfer");
            tx.commit();

            if (verbose) {
                std::cout << "[Success] Transferred Rs. " << std::fixed << std::setprecision(2) << amount
                          << " from Account #" << sourceId << " to Account #" << destId << "\n";
            }
            return true;

        } catch (const std::exception& ex) {
            if (verbose) {
                std::cout << "[Rolled Back] SQL exception: " << ex.what() << "\n";
            }
            return false;
        }
    }

    static void printAccounts() {
        auto& dbMgr = DatabaseManager::getInstance();
        std::lock_guard<std::mutex> lock(dbMgr.getMutex());
        sqlite3* db = dbMgr.getConnection();

        ScopedStatement stmt(db, "SELECT account_id, user_id, balance, account_type FROM Accounts ORDER BY account_id;");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            int accId = sqlite3_column_int(stmt.get(), 0);
            int uId = sqlite3_column_int(stmt.get(), 1);
            double bal = sqlite3_column_double(stmt.get(), 2);
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));

            auto acc = AccountFactory::createAccount(accId, uId, bal, type);
            acc->displayAccountDetails();
        }
    }

    static void printRecentTransactions(int limit = 5) {
        auto& dbMgr = DatabaseManager::getInstance();
        std::lock_guard<std::mutex> lock(dbMgr.getMutex());
        sqlite3* db = dbMgr.getConnection();

        ScopedStatement stmt(db,
            "SELECT transaction_id, source_account_id, destination_account_id, amount, status, timestamp "
            "FROM Transactions ORDER BY transaction_id DESC LIMIT ?;");
        sqlite3_bind_int(stmt.get(), 1, limit);

        std::cout << "\n--- Recent Transactions ---\n";
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            std::cout << "Tx #" << sqlite3_column_int(stmt.get(), 0)
                      << " | #" << sqlite3_column_int(stmt.get(), 1)
                      << " -> #" << sqlite3_column_int(stmt.get(), 2)
                      << " | Rs. " << std::fixed << std::setprecision(2) << sqlite3_column_double(stmt.get(), 3)
                      << " | " << reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 4))
                      << " | " << reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 5)) << "\n";
        }
    }
};

int main() {
    DatabaseManager::getInstance().resetBalances();

    std::cout << "--- Initial Account States ---\n";
    BankEngine::printAccounts();

    std::cout << "\n--- Running Transaction Tests ---\n";
    BankEngine::transferFunds(1, 2, 1500.00);
    BankEngine::transferFunds(1, 2, 3000.00); // should fail min balance check (3500 - 3000 < 1000)
    BankEngine::transferFunds(1, 2, -500.00);
    BankEngine::transferFunds(1, 999, 100.00);

    std::cout << "\n--- Running Concurrent Transfer Test (20 threads) ---\n";
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([]() { BankEngine::transferFunds(1, 2, 50.00, false); });
        threads.emplace_back([]() { BankEngine::transferFunds(2, 1, 50.00, false); });
    }
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "Completed 20 concurrent transfers.\n";

    std::cout << "\n--- Final Account States ---\n";
    BankEngine::printAccounts();
    BankEngine::printRecentTransactions(5);

    return 0;
}
