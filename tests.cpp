#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "securebank.hpp"

using namespace securebank;

static int g_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { ++g_failures; \
        std::cerr << "    FAILED: " #cond " (line " << __LINE__ << ")\n"; } } while (0)

#define CHECK_THROWS(stmt) \
    do { bool threw = false; \
        try { stmt; } catch (const std::exception&) { threw = true; } \
        if (!threw) { ++g_failures; \
            std::cerr << "    EXPECTED EXCEPTION: " #stmt " (line " << __LINE__ << ")\n"; } \
    } while (0)

static Money balanceOf(int id) { return BankEngine::getAccount(id)->getBalance(); }

static int countTransactions(const std::string& status) {
    Statement s(DatabaseManager::getInstance().connection(),
                "SELECT COUNT(*) FROM Transactions WHERE status = ?;");
    s.bindText(1, status); s.step();
    return static_cast<int>(s.columnInt(0));
}

static int countTransactionsByType(const std::string& type, const std::string& status) {
    Statement s(DatabaseManager::getInstance().connection(),
                "SELECT COUNT(*) FROM Transactions WHERE transaction_type = ? AND status = ?;");
    s.bindText(1, type).bindText(2, status); s.step();
    return static_cast<int>(s.columnInt(0));
}

static void test_successful_transfer() {
    auto r = BankEngine::transferFunds(1, 2, rupees(1500));
    CHECK(r.ok());
    CHECK(balanceOf(1) == rupees(3500));
    CHECK(balanceOf(2) == rupees(13500));
    CHECK(countTransactions("SUCCESS") == 1);
}

static void test_negative_amount_rejected() {
    auto r = BankEngine::transferFunds(1, 2, rupees(-500));
    CHECK(r.status == TransferStatus::InvalidAmount);
    CHECK(balanceOf(1) == rupees(5000));
    CHECK(balanceOf(2) == rupees(12000));
}

static void test_zero_amount_rejected() {
    CHECK(BankEngine::transferFunds(1, 2, 0).status == TransferStatus::InvalidAmount);
}

static void test_same_account_rejected() {
    CHECK(BankEngine::transferFunds(1, 1, rupees(10)).status == TransferStatus::SameAccount);
}

static void test_insufficient_funds_rejected_and_logged() {
    auto r = BankEngine::transferFunds(1, 2, rupees(6000));
    CHECK(r.status == TransferStatus::InsufficientFunds);
    CHECK(balanceOf(1) == rupees(5000));
    CHECK(countTransactions("FAILED") == 1);
}

static void test_savings_minimum_balance_enforced() {
    CHECK(BankEngine::transferFunds(1, 2, rupees(4600)).status == TransferStatus::InsufficientFunds);
    CHECK(BankEngine::transferFunds(1, 2, rupees(4500)).ok());
    CHECK(balanceOf(1) == rupees(500));
}

static void test_current_account_has_no_minimum_balance() {
    CHECK(BankEngine::transferFunds(2, 1, rupees(12000)).ok());
    CHECK(balanceOf(2) == 0);
}

static void test_per_transfer_limit() {
    CHECK(BankEngine::transferFunds(2, 1, rupees(11000)).ok());
    DatabaseManager::getInstance().execute(
        "UPDATE Accounts SET balance_paise = 9000000 WHERE account_id = 1;");
    CHECK(BankEngine::transferFunds(1, 2, rupees(60000)).status == TransferStatus::LimitExceeded);
}

static void test_missing_accounts_rejected() {
    CHECK(BankEngine::transferFunds(1, 99, rupees(100)).status == TransferStatus::DestinationNotFound);
    CHECK(balanceOf(1) == rupees(5000));
    CHECK(BankEngine::transferFunds(99, 1, rupees(100)).status == TransferStatus::SourceNotFound);
    CHECK(countTransactions("FAILED") == 2);
}

static void test_atomicity_rollback_on_midway_failure() {
    auto& dbm = DatabaseManager::getInstance();
    dbm.execute(
        "CREATE TRIGGER simulate_crash BEFORE INSERT ON Transactions WHEN NEW.amount_paise = 12345 "
        "BEGIN SELECT RAISE(ABORT, 'simulated crash'); END;");
    auto r = BankEngine::transferFunds(1, 2, 12345);
    dbm.execute("DROP TRIGGER simulate_crash;");
    CHECK(r.status == TransferStatus::DatabaseError);
    CHECK(balanceOf(1) == rupees(5000));
    CHECK(balanceOf(2) == rupees(12000));
}

static void test_database_refuses_negative_balance() {
    CHECK_THROWS(DatabaseManager::getInstance().execute(
        "UPDATE Accounts SET balance_paise = -1 WHERE account_id = 1;"));
}

static void test_database_refuses_non_positive_transaction_amount() {
    CHECK_THROWS(DatabaseManager::getInstance().execute(
        "INSERT INTO Transactions (source_account_id, destination_account_id, amount_paise,"
        " transaction_type, status) VALUES (1, 2, -5, 'TRANSFER', 'SUCCESS');"));
}

static void test_foreign_key_protects_history() {
    CHECK(BankEngine::transferFunds(1, 2, rupees(100)).ok());
    CHECK_THROWS(DatabaseManager::getInstance().execute(
        "DELETE FROM Users WHERE user_id = 1;"));
    CHECK(BankEngine::getAccount(1) != nullptr);
}

static void test_factory_rejects_unknown_type() {
    CHECK_THROWS(AccountFactory::createAccount(1, 1, 0, "Platinum"));
    CHECK(dynamic_cast<SavingsAccount*>(AccountFactory::createAccount(1, 1, 0, "Savings").get()) != nullptr);
    CHECK(dynamic_cast<CurrentAccount*>(AccountFactory::createAccount(1, 1, 0, "Current").get()) != nullptr);
}

static void test_polymorphic_rules_and_interest() {
    SavingsAccount s(1, 1, rupees(5000));
    CurrentAccount c(2, 2, rupees(5000));
    const Account* accounts[] = {&s, &c};
    CHECK(accounts[0]->minimumBalance() == rupees(500));
    CHECK(accounts[1]->minimumBalance() == 0);
    CHECK(accounts[0]->monthlyInterest() == 1666);
    CHECK(accounts[1]->monthlyInterest() == 0);
}

static void test_money_formatting() {
    CHECK(rupees(1500) == 150000);
    CHECK(rupees(0.1) + rupees(0.2) == rupees(0.3));
    CHECK(formatMoney(150050) == "Rs. 1500.50");
    CHECK(formatMoney(5) == "Rs. 0.05");
}

static void test_many_transfers_conserve_money() {
    const Money before = BankEngine::totalMoneyInSystem();
    for (int i = 0; i < 300; ++i) {
        BankEngine::transferFunds(1, 2, rupees(7 + i % 13));
        BankEngine::transferFunds(2, 1, rupees(5 + i % 11));
    }
    CHECK(BankEngine::totalMoneyInSystem() == before);
    CHECK(balanceOf(1) >= rupees(500));
}

static void test_concurrent_transfers_are_safe() {
    const Money before = BankEngine::totalMoneyInSystem();
    auto worker = [](int from, int to) {
        for (int i = 0; i < 250; ++i) BankEngine::transferFunds(from, to, rupees(10 + i % 7));
    };
    std::vector<std::thread> threads;
    threads.emplace_back(worker, 1, 2);
    threads.emplace_back(worker, 2, 1);
    threads.emplace_back(worker, 1, 2);
    threads.emplace_back(worker, 2, 1);
    for (auto& t : threads) t.join();
    CHECK(BankEngine::totalMoneyInSystem() == before);
    CHECK(balanceOf(1) >= rupees(500));
    CHECK(balanceOf(2) >= 0);
}

static void test_deposit_increases_balance() {
    Money before = balanceOf(1);
    CHECK(BankEngine::deposit(1, rupees(500)).ok());
    CHECK(balanceOf(1) == before + rupees(500));
}

static void test_deposit_logged_as_deposit_type() {
    BankEngine::deposit(1, rupees(100));
    CHECK(countTransactionsByType("DEPOSIT", "SUCCESS") == 1);
}

static void test_deposit_invalid_amount_rejected() {
    CHECK(BankEngine::deposit(1, rupees(0)).status   == TransferStatus::InvalidAmount);
    CHECK(BankEngine::deposit(1, rupees(-100)).status == TransferStatus::InvalidAmount);
    CHECK(balanceOf(1) == rupees(5000));
}

static void test_deposit_to_nonexistent_account() {
    CHECK(BankEngine::deposit(99, rupees(100)).status == TransferStatus::DestinationNotFound);
}

static void test_withdraw_decreases_balance() {
    Money before = balanceOf(2);
    CHECK(BankEngine::withdraw(2, rupees(1000)).ok());
    CHECK(balanceOf(2) == before - rupees(1000));
}

static void test_withdraw_minimum_balance_enforced() {
    CHECK(BankEngine::withdraw(1, rupees(4600)).status == TransferStatus::InsufficientFunds);
    CHECK(balanceOf(1) == rupees(5000));
    CHECK(BankEngine::withdraw(1, rupees(4500)).ok());
    CHECK(balanceOf(1) == rupees(500));
}

static void test_withdraw_logged_as_withdrawal_type() {
    BankEngine::withdraw(2, rupees(100));
    CHECK(countTransactionsByType("WITHDRAWAL", "SUCCESS") == 1);
}

static void test_withdraw_failed_attempt_logged() {
    BankEngine::withdraw(1, rupees(50000));
    CHECK(countTransactionsByType("WITHDRAWAL", "FAILED") == 1);
}

static void test_create_user_and_account() {
    int uid = BankEngine::createUser("Test", "User", "test@example.com");
    CHECK(uid > 0);
    int aid = BankEngine::createAccount(uid, "Savings", rupees(1000));
    CHECK(aid > 0);
    auto acc = BankEngine::getAccount(aid);
    CHECK(acc != nullptr);
    CHECK(acc->getBalance() == rupees(1000));
    CHECK(acc->getType() == "Savings");
    CHECK(acc->getUserId() == uid);
}

static void test_create_user_validates_input() {
    CHECK_THROWS(BankEngine::createUser("", "Kumar", "test@example.com"));
    CHECK_THROWS(BankEngine::createUser("Test", "", "test@example.com"));
    CHECK_THROWS(BankEngine::createUser("Test", "Kumar", "invalidemail"));
    CHECK_THROWS(BankEngine::createAccount(1, "Platinum", 0));
    CHECK_THROWS(BankEngine::createAccount(1, "Savings", -1));
}

static void test_duplicate_email_rejected() {
    CHECK_THROWS(BankEngine::createUser("Clone", "Khan", "azeem@dtu.ac.in"));
}

static void test_monthly_interest_posted_to_savings_only() {
    Money savingsBefore = balanceOf(1);
    Money currentBefore = balanceOf(2);
    Money totalInterest = BankEngine::postMonthlyInterest();
    CHECK(totalInterest > 0);
    CHECK(balanceOf(1) == savingsBefore + totalInterest);
    CHECK(balanceOf(2) == currentBefore);
    CHECK(countTransactionsByType("INTEREST", "SUCCESS") == 1);
}

static void test_interest_amount_is_correct() {
    CHECK(BankEngine::postMonthlyInterest() == 1666); // 500000 * 4 / 1200
}

static void test_transaction_history() {
    BankEngine::deposit(1, rupees(1000));
    BankEngine::transferFunds(1, 2, rupees(500));
    BankEngine::withdraw(1, rupees(200));
    auto history = BankEngine::getTransactionHistory(1);
    CHECK(history.size() == 3);
    CHECK(history[0].type == "DEPOSIT");
    CHECK(history[1].type == "TRANSFER");
    CHECK(history[2].type == "WITHDRAWAL");
    CHECK(history[0].sourceId == -1);
    CHECK(history[0].destId   == 1);
    CHECK(history[2].sourceId == 1);
    CHECK(history[2].destId   == -1);
}

int main() {
    DatabaseManager::setDatabasePath(":memory:");

    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"successful transfer",                        test_successful_transfer},
        {"negative amount rejected",                   test_negative_amount_rejected},
        {"zero amount rejected",                       test_zero_amount_rejected},
        {"same account rejected",                      test_same_account_rejected},
        {"insufficient funds rejected + logged",       test_insufficient_funds_rejected_and_logged},
        {"savings minimum balance enforced",           test_savings_minimum_balance_enforced},
        {"current account has no minimum",             test_current_account_has_no_minimum_balance},
        {"per-transfer limit",                         test_per_transfer_limit},
        {"missing accounts rejected",                  test_missing_accounts_rejected},
        {"atomicity: rollback on mid-way failure",     test_atomicity_rollback_on_midway_failure},
        {"DB refuses negative balance (CHECK)",        test_database_refuses_negative_balance},
        {"DB refuses non-positive txn amount (CHECK)", test_database_refuses_non_positive_transaction_amount},
        {"FK protects accounts with history",          test_foreign_key_protects_history},
        {"factory creates / rejects types",            test_factory_rejects_unknown_type},
        {"polymorphic rules + interest",               test_polymorphic_rules_and_interest},
        {"money helpers (integer paise)",              test_money_formatting},
        {"many transfers conserve total money",        test_many_transfers_conserve_money},
        {"concurrent transfers (4 threads)",           test_concurrent_transfers_are_safe},
        {"deposit increases balance",                  test_deposit_increases_balance},
        {"deposit logged as DEPOSIT type",             test_deposit_logged_as_deposit_type},
        {"deposit invalid amount rejected",            test_deposit_invalid_amount_rejected},
        {"deposit to nonexistent account rejected",    test_deposit_to_nonexistent_account},
        {"withdraw decreases balance",                 test_withdraw_decreases_balance},
        {"withdraw minimum balance enforced",          test_withdraw_minimum_balance_enforced},
        {"withdraw logged as WITHDRAWAL type",         test_withdraw_logged_as_withdrawal_type},
        {"withdraw failed attempt logged",             test_withdraw_failed_attempt_logged},
        {"createUser and createAccount",               test_create_user_and_account},
        {"createUser / createAccount validates input", test_create_user_validates_input},
        {"duplicate email rejected (UNIQUE)",          test_duplicate_email_rejected},
        {"monthly interest posted to Savings only",   test_monthly_interest_posted_to_savings_only},
        {"interest amount is correct (4% p.a./12)",   test_interest_amount_is_correct},
        {"getTransactionHistory per account",          test_transaction_history},
    };

    int failedTests = 0;
    for (const auto& [name, fn] : tests) {
        DatabaseManager::getInstance().resetDemoData();
        const int before = g_failures;
        try { fn(); }
        catch (const std::exception& e) {
            ++g_failures;
            std::cerr << "    UNEXPECTED EXCEPTION: " << e.what() << "\n";
        }
        const bool passed = (g_failures == before);
        if (!passed) ++failedTests;
        std::cout << (passed ? "[ PASS ] " : "[ FAIL ] ") << name << "\n";
    }

    std::cout << "\n" << (tests.size() - failedTests) << "/" << tests.size() << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
