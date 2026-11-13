#include <iostream>
#include <string>
#include "securebank.hpp"

using namespace std;
using namespace securebank;


void attempt(const string& label, const TransferResult& result) {
    string tag = result.ok() ? "[OK]      " : "[REJECTED]";
    cout << "  " << tag << " " << label
         << "\n             -> " << result.message << "\n";
}


void section(const string& title) {
    cout << "\n--- " << title << " ---\n";
}


int main() {
    try {
        cout << "=== SECUREBANK CORE TRANSACTION ENGINE ===\n";

        
        DatabaseManager::getInstance().resetDemoData();

        
        section("Accounts loaded from DB");
        BankEngine::printAllAccounts();

       
        section("Creating a new user and account");

        int priyaUserId = BankEngine::createUser("Priya", "Sharma", "priya@dtu.ac.in");
        int priyaAccId  = BankEngine::createAccount(priyaUserId, "Savings", rupees(0));

        cout << "  Created user    #" << priyaUserId << " (Priya Sharma)\n";
        cout << "  Created account #" << priyaAccId
             << " (Savings) for user #" << priyaUserId << "\n";

        
        try {
            BankEngine::createUser("", "X", "x@x.com");
        } catch (const invalid_argument& e) {
            cout << "  [VALIDATION] " << e.what() << "\n";
        }
        try {
            BankEngine::createAccount(priyaUserId, "Platinum", 0);
        } catch (const invalid_argument& e) {
            cout << "  [VALIDATION] " << e.what() << "\n";
        }

        
        section("Cash deposits");
        attempt("Deposit Rs. 10000  -> account #" + to_string(priyaAccId),
                BankEngine::deposit(priyaAccId, rupees(10000)));

        attempt("Deposit Rs. 0      -> account #1  (invalid amount)",
                BankEngine::deposit(1, rupees(0)));

        attempt("Deposit Rs. 100    -> account #99 (non-existent account)",
                BankEngine::deposit(99, rupees(100)));

        
        section("Fund transfers");
        attempt("Account 1 -> 2 : Rs. 1500  (valid)",
                BankEngine::transferFunds(1, 2, rupees(1500)));

        attempt("Account 1 -> 2 : Rs. -500  (negative amount)",
                BankEngine::transferFunds(1, 2, rupees(-500)));

        attempt("Account 1 -> 2 : Rs. 3100  (would break Rs. 500 minimum balance)",
                BankEngine::transferFunds(1, 2, rupees(3100)));

        attempt("Account 1 -> 99 : Rs. 100  (destination does not exist)",
                BankEngine::transferFunds(1, 99, rupees(100)));

        attempt("Account 1 -> 1  : Rs. 100  (same account)",
                BankEngine::transferFunds(1, 1, rupees(100)));

        attempt("Account 2 -> 1  : Rs. 600000 (over Current account transfer limit)",
                BankEngine::transferFunds(2, 1, rupees(600000)));

        
        section("Cash withdrawals");
        attempt("Withdraw Rs. 5000 from account #2",
                BankEngine::withdraw(2, rupees(5000)));

        attempt("Withdraw Rs. 9600 from account #" + to_string(priyaAccId) +
                " (would break minimum balance)",
                BankEngine::withdraw(priyaAccId, rupees(9600)));

        attempt("Withdraw Rs. 9500 from account #" + to_string(priyaAccId) +
                " (exactly at minimum — allowed)",
                BankEngine::withdraw(priyaAccId, rupees(9500)));

        
        section("Monthly interest posting");
        Money interest = BankEngine::postMonthlyInterest();
        cout << "  Total interest credited: " << formatMoney(interest) << "\n";

       
        section("Accounts after all operations");
        BankEngine::printAllAccounts();

        
        section("Transaction history for account #1");
        for (const auto& rec : BankEngine::getTransactionHistory(1)) {
            string src = (rec.sourceId == -1) ? "?" : to_string(rec.sourceId);
            string dst = (rec.destId   == -1) ? "?" : to_string(rec.destId);
            cout << "  #" << rec.id
                 << " | " << src << " -> " << dst
                 << " | " << formatMoney(rec.amount)
                 << " | " << rec.type
                 << " | " << rec.status;
            if (!rec.reason.empty()) cout << " (" << rec.reason << ")";
            cout << "\n";
        }

        
        section("Full audit log");
        BankEngine::printAuditLog();

        cout << "\nTotal money in system: "
             << formatMoney(BankEngine::totalMoneyInSystem()) << "\n";

        return 0;

    } catch (const exception& e) {
        cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }
}
