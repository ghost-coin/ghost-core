// Copyright (c) 2018-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <memory>

#include <boost/test/unit_test.hpp>

#include <fs.h>
#include <test/util/setup_common.h>
#include <wallet/bdb.h>
#include <wallet/db.h>
#include <util/translation.h>


BOOST_FIXTURE_TEST_SUITE(db_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(getwalletenv_file)
{
    std::string test_name = "test_name.dat";
    const fs::path datadir = GetDataDir();
    fs::path file_path = datadir / test_name;
#if BOOST_VERSION >= 107700
    std::ofstream f(BOOST_FILESYSTEM_C_STR(file_path));
#else
    std::ofstream f(file_path.BOOST_FILESYSTEM_C_STR);
#endif // BOOST_VERSION >= 107700
    f.close();

    std::string filename;
    std::shared_ptr<BerkeleyEnvironment> env = GetWalletEnv(file_path, filename);
    BOOST_CHECK(filename == test_name);
    BOOST_CHECK(env->Directory() == datadir);
}

BOOST_AUTO_TEST_CASE(getwalletenv_directory)
{
    std::string expected_name = "wallet.dat";
    const fs::path datadir = GetDataDir();

    std::string filename;
    std::shared_ptr<BerkeleyEnvironment> env = GetWalletEnv(datadir, filename);
    BOOST_CHECK(filename == expected_name);
    BOOST_CHECK(env->Directory() == datadir);
}

BOOST_AUTO_TEST_CASE(getwalletenv_g_dbenvs_multiple)
{
    fs::path datadir = GetDataDir() / "1";
    fs::path datadir_2 = GetDataDir() / "2";
    std::string filename;

    std::shared_ptr<BerkeleyEnvironment> env_1 = GetWalletEnv(datadir, filename);
    std::shared_ptr<BerkeleyEnvironment> env_2 = GetWalletEnv(datadir, filename);
    std::shared_ptr<BerkeleyEnvironment> env_3 = GetWalletEnv(datadir_2, filename);

    BOOST_CHECK(env_1 == env_2);
    BOOST_CHECK(env_2 != env_3);
}

BOOST_AUTO_TEST_CASE(getwalletenv_g_dbenvs_free_instance)
{
    fs::path datadir = GetDataDir() / "1";
    fs::path datadir_2 = GetDataDir() / "2";
    std::string filename;

    std::shared_ptr <BerkeleyEnvironment> env_1_a = GetWalletEnv(datadir, filename);
    std::shared_ptr <BerkeleyEnvironment> env_2_a = GetWalletEnv(datadir_2, filename);
    env_1_a.reset();

    std::shared_ptr<BerkeleyEnvironment> env_1_b = GetWalletEnv(datadir, filename);
    std::shared_ptr<BerkeleyEnvironment> env_2_b = GetWalletEnv(datadir_2, filename);

    BOOST_CHECK(env_1_a != env_1_b);
    BOOST_CHECK(env_2_a == env_2_b);
}


BOOST_AUTO_TEST_CASE(bdb_lifecycle_create_write_close_reopen_read)
{
    const fs::path path = GetDataDir() / "p1_bdb_lifecycle";
    const std::string key = "p1_lifecycle_marker";
    const std::string expected = "ghost-p1-bdb-persistence-ok";

    {
        DatabaseOptions options;
        options.require_create = true;
        options.require_format = DatabaseFormat::BERKELEY;

        DatabaseStatus status;
        bilingual_str error;
        auto database = MakeDatabase(path, options, status, error);

        BOOST_REQUIRE(database);
        BOOST_REQUIRE(status == DatabaseStatus::SUCCESS);
        BOOST_CHECK_EQUAL(database->Format(), "bdb");

        {
            auto batch = database->MakeBatch();
            BOOST_REQUIRE(batch);
            BOOST_REQUIRE(batch->Write(key, expected));
        }

        database->Flush();
        database->Close();
    }

    BOOST_REQUIRE(fs::exists(path / "wallet.dat"));

    {
        DatabaseOptions options;
        options.require_existing = true;
        options.require_format = DatabaseFormat::BERKELEY;

        DatabaseStatus status;
        bilingual_str error;
        auto database = MakeDatabase(path, options, status, error);

        BOOST_REQUIRE(database);
        BOOST_REQUIRE(status == DatabaseStatus::SUCCESS);
        BOOST_CHECK_EQUAL(database->Format(), "bdb");

        std::string actual;
        {
            auto batch = database->MakeBatch();
            BOOST_REQUIRE(batch);
            BOOST_REQUIRE(batch->Read(key, actual));
        }

        BOOST_CHECK_EQUAL(actual, expected);
        database->Close();
    }
}

BOOST_AUTO_TEST_CASE(bdb_rejects_invalid_existing_file)
{
    const fs::path path = GetDataDir() / "not_a_wallet_database.dat";
    std::ofstream file(path.string(), std::ios::binary);
    file << "not a Berkeley DB or SQLite database";
    file.close();

    DatabaseOptions options;
    options.require_existing = true;

    DatabaseStatus status;
    bilingual_str error;
    auto database = MakeDatabase(path, options, status, error);

    BOOST_CHECK(!database);
    BOOST_CHECK(status == DatabaseStatus::FAILED_BAD_FORMAT);
}

BOOST_AUTO_TEST_CASE(bdb_rejects_duplicate_load)
{
    const fs::path path = GetDataDir() / "duplicate_load";

    DatabaseOptions create_options;
    create_options.require_create = true;
    create_options.require_format = DatabaseFormat::BERKELEY;

    DatabaseStatus status;
    bilingual_str error;
    auto first = MakeDatabase(path, create_options, status, error);
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(status == DatabaseStatus::SUCCESS);

    auto first_batch = first->MakeBatch();
    BOOST_REQUIRE(first_batch);

    DatabaseOptions load_options;
    load_options.require_existing = true;
    load_options.require_format = DatabaseFormat::BERKELEY;

    auto second = MakeDatabase(path, load_options, status, error);
    BOOST_CHECK(!second);
    BOOST_CHECK(status == DatabaseStatus::FAILED_ALREADY_LOADED);

    first->Close();
}

BOOST_AUTO_TEST_SUITE_END()
