#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <fstream>
#include <stdexcept>
#include <algorithm>

// ==========================================
// 1. OOP PILLARS: ABSTRACT CLASS & INHERITANCE
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
    virtual void displayAccountDetails() const = 0;
    
    int getAccountId() const { return accountId; }
    double getBalance() const { return balance; }
    
    void updateBalance(double amount) {
        if (balance + amount < 0) {
            throw std::runtime_error("Insufficient balance!");
        }
        balance += amount;
    }
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
// 2. FACTORY PATTERN: OBJECT CREATION
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
// 3. TRANSACTION ENGINE & STORAGE SIMULATOR
// ==========================================
class BankEngine {
private:
    std::vector<std::unique_ptr<Account>> accounts;

    BankEngine() {
        // Initializing dummy database records
        accounts.push_back(AccountFactory::createAccount(1, 101, 5000.0, "Savings"));
        accounts.push_back(AccountFactory::createAccount(2, 102, 12000.0, "Current"));
    }

public:
    static BankEngine& getInstance() {
        static BankEngine instance;
        return instance;
    }

    void showAllAccounts() const {
        std::cout << "\n--- Current Database Account States ---" << std::endl;
        for (const auto& acc : accounts) {
            acc->displayAccountDetails();
        }
    }

    Account* findAccount(int accId) {
        for (auto& acc : accounts) {
            if (acc->getAccountId() == accId) {
                return acc.get();
            }
        }
        return nullptr;
    }

    void transferFunds(int sourceId, int destId, double amount) {
        if (sourceId == destId) {
            std::cerr << "[ERROR] Source and Destination cannot be the same." << std::endl;
            return;
        }

        Account* source = findAccount(sourceId);
        Account* dest = findAccount(destId);

        if (!source || !dest) {
            std::cerr << "[ERROR] Transaction Failed: Account not found!" << std::endl;
            return;
        }

        try {
            // ACID Simulation: Check and update atomically
            if (source->getBalance() < amount) {
                throw std::runtime_error("Insufficient funds in source account!");
            }

            source->updateBalance(-amount);
            dest->updateBalance(amount);

            // Log transaction to a file (DBMS durability simulation)
            std::ofstream logFile("secure_bank_log.txt", std::ios::app);
            if (logFile.is_open()) {
                logFile << "SUCCESS: Transferred Rs. " << amount 
                        << " from Account #" << sourceId 
                        << " to Account #" << destId << "\n";
                logFile.close();
            }

            std::cout << "[SUCCESS] Successfully transferred Rs. " << amount 
                      << " from Account #" << sourceId << " to Account #" << destId << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "[TRANSACTION ROLLED BACK] Reason: " << e.what() << std::endl;
        }
    }
};

// ==========================================
// 4. MAIN APPLICATION DRIVER
// ==========================================
int main() {
    std::cout << "=== SECUREBANK CORE ENGINE INITIALIZED ===" << std::endl;

    BankEngine& bank = BankEngine::getInstance();
    
    // Display initial balances
    bank.showAllAccounts();

    // Execute atomic transaction test
    std::cout << "\nExecuting atomic transaction (Transferring Rs. 1500.00)..." << std::endl;
    bank.transferFunds(1, 2, 1500.00);

    // Display updated balances
    bank.showAllAccounts();

    return 0;
}