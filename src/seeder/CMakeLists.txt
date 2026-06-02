# Copyright (c) 2017-2024 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

project(bitcoin-seeder)

add_library(seeder bitcoin.cpp db.cpp dns.cpp util.cpp)
target_link_libraries(seeder common bitcoinconsensus)

add_executable(bitcoin-seeder main.cpp)
# DeVault rebrand: emit the binary as `devault-seeder` (target name unchanged).
set_target_properties(bitcoin-seeder PROPERTIES OUTPUT_NAME devault-seeder)
target_link_libraries(bitcoin-seeder common bitcoinconsensus seeder)

include(BinaryTest)
add_to_symbols_check(bitcoin-seeder)
add_to_security_check(bitcoin-seeder)

include(InstallationHelper)
install_target(bitcoin-seeder)

if(ENABLE_TEST)
    add_subdirectory(test)
endif()
