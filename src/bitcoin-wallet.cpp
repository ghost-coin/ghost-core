// Copyright (c) 2016-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <chainparams.h>
#include <chainparamsbase.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/system.h>
#include <common/globals.h>
#include <common/url.h>
#include <compat/compat.h>
#include <interfaces/init.h>
#include <key.h>
#include <logging.h>
#include <pubkey.h>
#include <tinyformat.h>
#include <util/exception.h>
#include <util/translation.h>
#include <wallet/wallettool.h>

#include <exception>
#include <functional>
#include <string>
#include <tuple>

// Particl includes
#include <key/mnemonic.h>

const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;
UrlDecodeFn* const URL_DECODE = nullptr;

static void SetupWalletToolArgs(ArgsManager& argsman)
{
    SetupHelpOptions(argsman);
    SetupChainParamsBaseOptions(argsman);

    argsman.AddArg("-version", "Print version and exit", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-datadir=<dir>", "Specify data directory", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-wallet=<wallet-name>", "Specify wallet name", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::OPTIONS);
    argsman.AddArg("-dumpfile=<file name>", "When used with 'dump', writes out the records to this file. When used with 'createfromdump', loads the records into a new wallet.", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION, OptionsCategory::OPTIONS);
    argsman.AddArg("-debug=<category>", "Output debugging information (default: 0).", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-descriptors", "Create descriptors wallet. Only for 'create'", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-legacy", "Create legacy wallet. Only for 'create'", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-format=<format>", "The format of the wallet file to create. Either \"bdb\" or \"sqlite\". Only used with 'createfromdump'", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-printtoconsole", "Send trace/debug info to console (default: 1 when no -debug is true, 0 otherwise).", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);

    argsman.AddCommand("info", "Get wallet info");
    argsman.AddCommand("create", "Create new wallet file");
    argsman.AddCommand("salvage", "Attempt to recover private keys from a corrupt wallet. Warning: 'salvage' is experimental.");
    argsman.AddCommand("dump", "Print out all of the wallet key-value records");
    argsman.AddCommand("createfromdump", "Create new wallet file from dumped records");

    // Particl
    argsman.AddCommand("generatemnemonic", "Generate a new mnemonic: <language> <bytes_entropy>");
    argsman.AddArg("-btcmode", "", ArgsManager::ALLOW_ANY, OptionsCategory::HIDDEN);
}

static std::optional<int> WalletAppInit(ArgsManager& args, int argc, char* argv[])
{
    SetupWalletToolArgs(args);
    std::string error_message;
    if (!args.ParseParameters(argc, argv, error_message)) {
        tfm::format(std::cerr, "Error parsing command line arguments: %s\n", error_message);
        return EXIT_FAILURE;
    }
    const bool missing_args{argc < 2};
    if (missing_args || HelpRequested(args) || args.IsArgSet("-version")) {
        std::string strUsage = strprintf("%s ghost-wallet version", PACKAGE_NAME) + " " + FormatFullVersion() + "\n";

        if (args.IsArgSet("-version")) {
            strUsage += FormatParagraph(LicenseInfo());
        } else {
            strUsage += "\n"
                        "ghost-wallet is an offline tool for creating and interacting with " PACKAGE_NAME " wallet files.\n"
                        "By default ghost-wallet will act on wallets in the default mainnet wallet directory in the datadir.\n"
                        "To change the target wallet, use the -datadir, -wallet and -regtest/-signet/-testnet arguments.\n\n"
                        "Usage:\n"
                        "  ghost-wallet [options] <command>\n";
            strUsage += "\n" + args.GetHelpMessage();
        }
        tfm::format(std::cout, "%s", strUsage);
        if (missing_args) {
            tfm::format(std::cerr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    fParticlMode = !gArgs.GetBoolArg("-btcmode", false); // qa tests

    // check for printtoconsole, allow -debug
    LogInstance().m_print_to_console = args.GetBoolArg("-printtoconsole", args.GetBoolArg("-debug", false));

    if (!CheckDataDirOption(args)) {
        tfm::format(std::cerr, "Error: Specified data directory \"%s\" does not exist.\n", args.GetArg("-datadir", ""));
        return EXIT_FAILURE;
    }
    // Check for chain settings (Params() calls are only valid after this clause)
    SelectParams(args.GetChainType());
    if (!fParticlMode) {
        WITNESS_SCALE_FACTOR = WITNESS_SCALE_FACTOR_BTC;
        if (args.GetChainType() == ChainType::REGTEST) {
            ResetParams(ChainType::REGTEST, fParticlMode);
        }
    }

    return std::nullopt;
}

MAIN_FUNCTION
{
    ArgsManager& args = gArgs;
#ifdef WIN32
    common::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif

    int exit_status;
    std::unique_ptr<interfaces::Init> init = interfaces::MakeWalletInit(argc, argv, exit_status);
    if (!init) {
        return exit_status;
    }

    SetupEnvironment();
    RandomInit();


    bool show_help = false;
    for (int i = 1; i < argc; ++i) {
        if (IsSwitchChar(argv[i][0])) {
            char *p = argv[i];
            while (*p == '-') p++;
            if (strcmp(p, "?") == 0 || strcmp(p, "h") == 0 || strcmp(p, "help") == 0) {
                show_help = true;
            }
            continue;
        }
        if (strcmp(argv[i], "generatemnemonic") == 0) {
            if (show_help) {
                std::string usage = "generatemnemonic <language> <bytes_entropy>\n"
                    "\nArguments:\n"
                    "1. language        (string, optional, default=english) Which wordlist to use (" + mnemonic::ListEnabledLanguages(", ") + ").\n"
                    "2. bytes_entropy   (numeric, optional, default=32) Affects length of mnemonic, [16, 64].\n";
                tfm::format(std::cout, "%s\n", usage);
                return EXIT_SUCCESS;
            }

            int nLanguage = mnemonic::WLL_ENGLISH;
            int nBytesEntropy = 32;

            if (argc > i + 1) {
                nLanguage = mnemonic::GetLanguageOffset(argv[i+1]);
            }
            if (argc > i + 2) {
                if (!ParseInt32(argv[i+2], &nBytesEntropy)) {
                    tfm::format(std::cerr, "Error: Invalid num bytes entropy.\n");
                    return EXIT_FAILURE;
                }
                if (nBytesEntropy < 16 || nBytesEntropy > 64) {
                    tfm::format(std::cerr, "Error: Num bytes entropy out of range [16,64].\n");
                    return EXIT_FAILURE;
                }
            }
            std::string sMnemonic, sError;
            std::vector<uint8_t> vEntropy(nBytesEntropy);

            GetStrongRandBytes2(&vEntropy[0], nBytesEntropy);
            if (0 != mnemonic::Encode(nLanguage, vEntropy, sMnemonic, sError)) {
                tfm::format(std::cerr, "Error: MnemonicEncode failed %s.\n", sError);
                return EXIT_FAILURE;
            }

            tfm::format(std::cout, "%s\n", sMnemonic);
            return EXIT_SUCCESS;
        }
    }


    try {
        if (const auto maybe_exit{WalletAppInit(args, argc, argv)}) return *maybe_exit;
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "WalletAppInit()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "WalletAppInit()");
        return EXIT_FAILURE;
    }

    const auto command = args.GetCommand();
    if (!command) {
        tfm::format(std::cerr, "No method provided. Run `ghost-wallet -help` for valid methods.\n");
        return EXIT_FAILURE;
    }
    if (command->args.size() != 0) {
        tfm::format(std::cerr, "Error: Additional arguments provided (%s). Methods do not take arguments. Please refer to `-help`.\n", Join(command->args, ", "));
        return EXIT_FAILURE;
    }

    ECC_Start();
    if (!wallet::WalletTool::ExecuteWalletToolFunc(args, command->command)) {
        return EXIT_FAILURE;
    }
    ECC_Stop();
    return EXIT_SUCCESS;
}
