#include <iostream>
#include <bits/stdc++.h>
#include "securebank.hpp"

using namespace securebank;

static void attempt(const std::string& label, const TransferResult& r) {
    std::cout << "  " << (r.ok() ? "[OK]       " : "[REJECTED] ") << label
              << "\n             -> " << r.message << "\n";
}

static void section(const std::string& title) {
    std::cout << "\n--- " << title << " ---\n";
}

int main() {
    try {
        std::cout << "=== SECUREBANK CORE TRANSACTION ENGINE ===\n";

        DatabaseManager::getInstance().resetDemoData();

        section("Accounts loaded from DB");
        BankEngine::printAllAccounts();

        section("Creating a new user and account");
        int priyaId    = BankEngine::createUser("Priya", "Sharma", "priya@dtu.ac.in");
        int priyaAccId = BankEngine::createAccount(priyaId, "Savings", rupees(0));
        std::cout << "  Created user #" << priyaId << " (Priya Sharma)\n";
        std::cout << "  Created Savings account #" << priyaAccId << " for user #" << priyaId << "\n";

        try { BankEngine::createUser("", "X", "x@x.com"); }
        catch (const std::invalid_argument& e) {
            std::cout << "  [VALIDATION] " << e.what() << "\n";
        }
        try { BankEngine::createAccount(priyaId, "Platinum", 0); }
        catch (const std::invalid_argument& e) {
            std::cout << "  [VALIDATION] " << e.what() << "\n";
        }

        section("Cash deposits");
        attempt("Deposit Rs. 10000.00 -> account #" + std::to_string(priyaAccId),
                BankEngine::deposit(priyaAccId, rupees(10000)));
        attempt("Deposit Rs.     0.00 -> account #1 (invalid amount)",
                BankEngine::deposit(1, rupees(0)));
        attempt("Deposit Rs.   100.00 -> account #99 (non-existent)",
                BankEngine::deposit(99, rupees(100)));

        section("Fund transfers");
        attempt("1 -> 2 : Rs. 1500.00",
                BankEngine::transferFunds(1, 2, rupees(1500)));
        attempt("1 -> 2 : Rs. -500.00 (negative amount)",
                BankEngine::transferFunds(1, 2, rupees(-500)));
        attempt("1 -> 2 : Rs. 3100.00 (would break Rs. 500 minimum)",
                BankEngine::transferFunds(1, 2, rupees(3100)));
        attempt("1 -> 99 : Rs. 100.00 (destination does not exist)",
                BankEngine::transferFunds(1, 99, rupees(100)));
        attempt("1 -> 1 : Rs. 100.00 (same account)",
                BankEngine::transferFunds(1, 1, rupees(100)));
        attempt("2 -> 1 : Rs. 600000.00 (over Current account limit)",
                BankEngine::transferFunds(2, 1, rupees(600000)));

        section("Cash withdrawals");
        attempt("Withdraw Rs. 5000.00 from account #2",
                BankEngine::withdraw(2, rupees(5000)));
        attempt("Withdraw Rs. 9600.00 from account #" + std::to_string(priyaAccId) + " (minimum enforced)",
                BankEngine::withdraw(priyaAccId, rupees(9600)));
        attempt("Withdraw Rs. 9500.00 from account #" + std::to_string(priyaAccId) + " (exactly at minimum)",
                BankEngine::withdraw(priyaAccId, rupees(9500)));

        section("Monthly interest posting");
        Money interest = BankEngine::postMonthlyInterest();
        std::cout << "  Total interest credited: " << formatMoney(interest) << "\n";

        section("Accounts after all operations");
        BankEngine::printAllAccounts();

        section("Transaction history for account #1");
        for (const auto& rec : BankEngine::getTransactionHistory(1)) {
            std::string src = rec.sourceId == -1 ? "?" : std::to_string(rec.sourceId);
            std::string dst = rec.destId   == -1 ? "?" : std::to_string(rec.destId);
            std::cout << "  #" << rec.id << " | " << src << " -> " << dst << " | "
                      << formatMoney(rec.amount) << " | " << rec.type << " | " << rec.status;
            if (!rec.reason.empty()) std::cout << " (" << rec.reason << ")";
            std::cout << "\n";
        }

        section("Full audit log");
        BankEngine::printAuditLog();

        std::cout << "\nTotal money in system: " << formatMoney(BankEngine::totalMoneyInSystem()) << "\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
}
