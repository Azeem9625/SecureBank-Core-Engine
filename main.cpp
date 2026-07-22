#include <iostream>
#include <memory>
#include <string>
#include <stdexcept>
#include "sqlite3.h"

// ==========================================
// 1. SINGLETON PATTERN: DATABASE MANAGER
// ==========================================
class DatabaseManager {
private:
    sqlite3* db;
    DatabaseManager() {
        // Automatically creates a local database file in your project folder
        int exitCode = sqlite3_open("secure_bank.db", &db);
        if (exitCode) {
            std::cerr << "[FATAL] Can't open database: " << sqlite3_errmsg(db) << std::endl;
            exit(1);
        }
        
        // Enable Foreign Keys & Write-Ahead Logging (WAL) for concurrency & safety
        sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        
        initializeDatabaseSchema();
    }

    void initializeDatabaseSchema() {
        const char* sql = 
            // Normalized Users Table (3NF)
            "CREATE TABLE IF NOT EXISTS Users ("
            "user_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "first_name TEXT NOT NULL, "
            "last_name TEXT NOT NULL, "
            "email TEXT UNIQUE NOT NULL);"

            // Normalized Accounts Table with CHECK constraint for safety
            "CREATE TABLE IF NOT EXISTS Accounts ("
            "account_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "user_id INTEGER NOT NULL, "
            "account_type TEXT NOT NULL, "
            "balance REAL NOT NULL CHECK(balance >= 0.0), "
            "FOREIGN KEY(user_id) REFERENCES Users(user_id) ON DELETE CASCADE);"

            // Relational Audit Trail (Transactions Table)
            "CREATE TABLE IF NOT EXISTS Transactions ("
            "transaction_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "source_account_id INTEGER, "
            "destination_account_id INTEGER, "
            "amount REAL NOT NULL, "
            "status TEXT NOT NULL, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
            "FOREIGN KEY(source_account_id) REFERENCES Accounts(account_id), "
            "FOREIGN KEY(destination_account_id) REFERENCES Accounts(account_id));"

            // Insert initial test records if tables are empty
            "INSERT OR IGNORE INTO Users (user_id, first_name, last_name, email) VALUES (1, 'Azeem', 'Khan', 'azeem@dtu.ac.in');"
            "INSERT OR IGNORE INTO Users (user_id, first_name, last_name, email) VALUES (2, 'Raja', 'Kumar', 'raja@dtu.ac.in');"
            "INSERT OR IGNORE INTO Accounts (account_id, user_id, account_type, balance) VALUES (1, 1, 'Savings', 5000.0);"
            "INSERT OR IGNORE INTO Accounts (account_id, user_id, account_type, balance) VALUES (2, 2, 'Current', 12000.0);";

        char* errMsg = nullptr;
        sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
        if (errMsg) {
            std::cerr << "Database Schema Setup Error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        }
    }

public:
    static DatabaseManager& getInstance() {
        static DatabaseManager instance;
        return instance;
    }

    sqlite3* getConnection() { return db; }

    ~DatabaseManager() {
        if (db) sqlite3_close(db);
    }

    DatabaseManager(const DatabaseManager&) = delete;
    void operator=(const DatabaseManager&) = delete;
};

// ==========================================
// 2. OOP PILLARS: ABSTRACT CLASS & INHERITANCE
// ==========================================
class Account {
protected:
    int accountId;
    int userId;
    double balance;
    std::string accountType;

public:
    Account(int accId, int uId, double bal, std::string type)
        : accountId(accId), userId(uId), balance(bal), accountType(type) {}
    
    virtual ~Account() = default;
    virtual void displayAccountDetails() const = 0; // Polymorphism
    
    int getAccountId() const { return accountId; }
    double getBalance() const { return balance; }
};

class SavingsAccount : public Account {
public:
    SavingsAccount(int accId, int uId, double bal) : Account(accId, uId, bal, "Savings") {}
    
    void displayAccountDetails() const override {
        std::cout << "[Savings Account] ID: " << accountId 
                  << " | User ID: " << userId 
                  << " | Balance: Rs. " << balance << std::endl;
    }
};

class CurrentAccount : public Account {
public:
    CurrentAccount(int accId, int uId, double bal) : Account(accId, uId, bal, "Current") {}
    
    void displayAccountDetails() const override {
        std::cout << "[Current Account] ID: " << accountId 
                  << " | User ID: " << userId 
                  << " | Balance: Rs. " << balance << std::endl;
    }
};

// ==========================================
// 3. FACTORY PATTERN: OBJECT CREATION
// ==========================================
class AccountFactory {
public:
    static std::unique_ptr<Account> createAccount(int accId, int uId, double bal, const std::string& type) {
        if (type == "Savings") {
            return std::make_unique<SavingsAccount>(accId, uId, bal);
        } else if (type == "Current") {
            return std::make_unique<CurrentAccount>(accId, uId, bal);
        }
        throw std::invalid_argument("Invalid Account Type specified!");
    }
};

// ==========================================
// 4. TRANSACTION ENGINE (DBMS ACID COMPLIANCE)
// ==========================================
class BankEngine {
public:
    static void transferFunds(int sourceId, int destId, double amount) {
        if (sourceId == destId) {
            std::cerr << "[ERROR] Source and Destination accounts cannot be the same." << std::endl;
            return;
        }

        sqlite3* db = DatabaseManager::getInstance().getConnection();
        char* errMsg = nullptr;

        // 1. BEGIN ACID TRANSACTION (Atomicity & Isolation boundary)
        if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "[ERROR] Failed to start transaction: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return;
        }

        // 2. Fetch and check source balance using prepared statements to prevent injection
        std::string balanceQuery = "SELECT balance FROM Accounts WHERE account_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        double sourceBal = -1.0;

        if (sqlite3_prepare_v2(db, balanceQuery.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, sourceId);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                sourceBal = sqlite3_column_double(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }

        if (sourceBal < 0 || sourceBal < amount) {
            std::cerr << "[TRANSACTION ROLLED BACK] Reason: Insufficient funds or invalid source account!" << std::endl;
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return;
        }

        // 3. Perform Atomic Updates on Accounts Table
        std::string updateSource = "UPDATE Accounts SET balance = balance - " + std::to_string(amount) + " WHERE account_id = " + std::to_string(sourceId) + ";";
        std::string updateDest = "UPDATE Accounts SET balance = balance + " + std::to_string(amount) + " WHERE account_id = " + std::to_string(destId) + ";";
        std::string logTransaction = "INSERT INTO Transactions (source_account_id, destination_account_id, amount, status) VALUES (" + std::to_string(sourceId) + ", " + std::to_string(destId) + ", " + std::to_string(amount) + ", 'SUCCESS');";

        bool executionFailed = false;
        if (sqlite3_exec(db, updateSource.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) executionFailed = true;
        if (!executionFailed && sqlite3_exec(db, updateDest.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) executionFailed = true;
        if (!executionFailed && sqlite3_exec(db, logTransaction.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) executionFailed = true;

        if (executionFailed) {
            std::cerr << "[SQL ERROR - ROLLED BACK]: " << (errMsg ? errMsg : "Unknown error") << std::endl;
            if (errMsg) sqlite3_free(errMsg);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        } else {
            // 4. COMMIT TRANSACTION (Durability & Consistency achieved)
            sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
            std::cout << "[SUCCESS] Successfully transferred Rs. " << amount 
                      << " from Account #" << sourceId << " to Account #" << destId << std::endl;
        }
    }
};

// ==========================================
// 5. MAIN APPLICATION DRIVER
// ==========================================
int main() {
    std::cout << "=== SECUREBANK CORE DBMS ENGINE INITIALIZED ===" << std::endl;

    sqlite3* db = DatabaseManager::getInstance().getConnection();

    // Fetch and display current state directly from the Database using Polymorphism & Factory Pattern
    const char* query = "SELECT account_id, user_id, balance, account_type FROM Accounts;";
    sqlite3_stmt* stmt = nullptr;

    std::cout << "\n--- Fetching Account States from Database ---" << std::endl;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int accId = sqlite3_column_int(stmt, 0);
            int uId = sqlite3_column_int(stmt, 1);
            double bal = sqlite3_column_double(stmt, 2);
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            auto acc = AccountFactory::createAccount(accId, uId, bal, type);
            acc->displayAccountDetails();
        }
        sqlite3_finalize(stmt);
    }

    // Execute atomic transaction test
    std::cout << "\nExecuting atomic transaction (Transferring Rs. 1500.00)..." << std::endl;
    BankEngine::transferFunds(1, 2, 1500.00);

    // Fetch updated states from DB again
    std::cout << "\n--- Updated Account States in Database ---" << std::endl;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int accId = sqlite3_column_int(stmt, 0);
            int uId = sqlite3_column_int(stmt, 1);
            double bal = sqlite3_column_double(stmt, 2);
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            auto acc = AccountFactory::createAccount(accId, uId, bal, type);
            acc->displayAccountDetails();
        }
        sqlite3_finalize(stmt);
    }

    return 0;
}
