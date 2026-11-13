cmake_minimum_required(VERSION 3.10)
project(SecureBank CXX C)

# ── C++ Standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compiler Warnings ─────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── SQLite (bundled amalgamation — no external install needed) ────────────────
add_library(sqlite3 STATIC sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# On Linux/macOS, SQLite needs -ldl and -lpthread
if(UNIX)
    target_link_libraries(sqlite3 PUBLIC dl pthread)
endif()

# ── Demo Executable: securebank ───────────────────────────────────────────────
add_executable(securebank main.cpp)
target_include_directories(securebank PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(securebank PRIVATE sqlite3)

if(WIN32)
    # Windows needs no extra libs beyond sqlite3
else()
    target_link_libraries(securebank PRIVATE pthread)
endif()

# ── Test Executable: securebank_tests ─────────────────────────────────────────
add_executable(securebank_tests tests.cpp)
target_include_directories(securebank_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(securebank_tests PRIVATE sqlite3)

if(WIN32)
    # pthreads bundled on Windows via MinGW or MSVC runtime
else()
    target_link_libraries(securebank_tests PRIVATE pthread)
endif()

# ── CTest Integration ─────────────────────────────────────────────────────────
enable_testing()

add_test(
    NAME    SecureBankTests
    COMMAND securebank_tests
)

# Mark test as passing only when exit code is 0
set_tests_properties(SecureBankTests PROPERTIES
    PASS_REGULAR_EXPRESSION "32/32 tests passed"
)
