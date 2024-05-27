#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <argparse/argparse.hpp>
#include <indicators/indeterminate_progress_bar.hpp>
#include <vpkedit/detail/Misc.h>
#include <vpkedit/format/VPK.h>
#include <vpkedit/Version.h>

using namespace std::literals::string_literals;
using namespace vpkedit::detail;
using namespace vpkedit;

constexpr std::string_view ARG_OUTPUT_SHORT = "-o";
constexpr std::string_view ARG_OUTPUT_LONG = "--output";
constexpr std::string_view ARG_NO_PROGRESS_LONG = "--no-progress";
constexpr std::string_view ARG_VERSION_SHORT = "-v";
constexpr std::string_view ARG_VERSION_LONG = "--version";
constexpr std::string_view ARG_CHUNKSIZE_SHORT = "-c";
constexpr std::string_view ARG_CHUNKSIZE_LONG = "--chunksize";
constexpr std::string_view ARG_GEN_MD5_ENTRIES_LONG = "--gen-md5-entries";
constexpr std::string_view ARG_PRELOAD_SHORT = "-p";
constexpr std::string_view ARG_PRELOAD_LONG = "--preload";
constexpr std::string_view ARG_SINGLE_FILE_SHORT = "-s";
constexpr std::string_view ARG_SINGLE_FILE_LONG = "--single-file";
constexpr std::string_view ARG_GEN_KEYPAIR_LONG = "--gen-keypair";
constexpr std::string_view ARG_FILE_TREE_LONG = "--file-tree";
constexpr std::string_view ARG_SIGN_SHORT = "-k";
constexpr std::string_view ARG_SIGN_LONG = "--sign";
constexpr std::string_view ARG_VERIFY_CHECKSUMS_LONG = "--verify-checksums";
constexpr std::string_view ARG_VERIFY_SIGNATURE_LONG = "--verify-signature";

namespace {

/// Print the file tree of an existing VPK
void fileTree(const std::string& inputPath) {
	auto vpk = VPK::open(inputPath);
	if (!vpk) {
		std::cerr << "Could not print the file tree of VPK at \"" << inputPath << "\": it failed to load!" << std::endl;
	}

	// todo: make this more tree-like
	for (const auto& [directory, entries] : vpk->getBakedEntries()) {
		std::cout << directory << std::endl;
		for (const auto& entry : entries) {
			std::cout << "- " << entry.getFilename() << std::endl;
		}
	}
}

/// Sign an existing VPK
void sign(const argparse::ArgumentParser& cli, const std::string& inputPath) {
	auto saveToDir = cli.get<bool>(ARG_SINGLE_FILE_SHORT);
	auto signPath = cli.is_used(ARG_SIGN_SHORT) ? cli.get(ARG_SIGN_SHORT) : "";

	if (saveToDir) {
		std::cerr << "Warning: Signed VPKs that contain files will not be treated as signed by the Source engine!" << std::endl;
		std::cerr << "Remove the " << ARG_SINGLE_FILE_SHORT << " / " << ARG_SINGLE_FILE_LONG << " parameter for best results." << std::endl;
	}

	auto vpk = VPK::open(inputPath);
	if (!vpk || !dynamic_cast<VPK*>(vpk.get())->sign(signPath)) {
		std::cerr << "Failed to sign VPK using private key file at \"" << signPath << "\"!" << std::endl;
		std::cerr << "Check that the file exists and it contains both the private key and public key." << std::endl;
	} else {
		std::cout << "Signed VPK using private key at \"" << signPath << "\"." << std::endl;
	}
}

/// Verify checksums and/or signature are valid for an existing VPK
void verify(const argparse::ArgumentParser& cli, const std::string& inputPath) {
	auto vpk = VPK::open(inputPath);
	if (!vpk) {
		std::cerr << "Could not verify VPK at \"" << inputPath << "\": it failed to load!" << std::endl;
		return;
	}

	if (cli.is_used(ARG_VERIFY_CHECKSUMS_LONG)) {
		if (cli.get(ARG_VERIFY_CHECKSUMS_LONG) == "all" || cli.get(ARG_VERIFY_CHECKSUMS_LONG) == "vpk") {
			if (vpk->verifyFileChecksum()) {
				std::cout << "Overall VPK checksums match their expected values." << std::endl;
			} else {
				std::cerr << "One or more of the VPK checksums do not match the expected value(s)!" << std::endl;
			}
		}
		if (cli.get(ARG_VERIFY_CHECKSUMS_LONG) == "all" || cli.get(ARG_VERIFY_CHECKSUMS_LONG) == "files") {
			if (auto entries = vpk->verifyEntryChecksums(); entries.empty()) {
				std::cout << "All file checksums match their expected values." << std::endl;
			} else {
				std::cerr << "Some file checksums do not match their expected values!" << std::endl;
				std::cerr << "Files that failed to validate:" << std::endl;
				for (const auto& entryPath : entries) {
					std::cerr << entryPath << std::endl;
				}
			}
		}
	}

	if (cli.is_used(ARG_VERIFY_SIGNATURE_LONG)) {
		if (!vpk->hasFileSignature()) {
			std::cout << "VPK does not have a signature." << std::endl;
		} else if (vpk->verifyFileChecksum()) {
			std::cout << "VPK signature is valid." << std::endl;
		} else {
			std::cerr << "VPK signature is invalid!" << std::endl;
		}
	}
}

/// Pack contents of a directory into a VPK
void pack(const argparse::ArgumentParser& cli, const std::string& inputPath) {
	auto outputPath = inputPath + (cli.get<bool>("-s") || inputPath.ends_with("_dir") ? ".vpk" : "_dir.vpk");
	if (cli.is_used(ARG_OUTPUT_SHORT)) {
		if (!cli.get(ARG_OUTPUT_SHORT).ends_with(".vpk")) {
			throw std::runtime_error("Output path must be a VPK file!");
		}
		outputPath = cli.get("-o");
		if (!cli.get<bool>("-s") && !outputPath.ends_with("_dir.vpk")) {
			std::cerr << "Warning: multichunk VPK is being written without a \"_dir\" suffix (e.g. \"hl2_textures_dir.vpk\").\n"
			             "This VPK may not be able to be loaded by the Source engine or other VPK browsers!\n" << std::endl;
		}
	}

	auto noProgressBar = cli.get<bool>(ARG_NO_PROGRESS_LONG);
	auto version = static_cast<std::uint32_t>(std::stoi(cli.get(ARG_VERSION_SHORT)));
	auto preferredChunkSize = static_cast<std::uint32_t>(std::stoi(cli.get(ARG_CHUNKSIZE_SHORT)) * 1024 * 1024);
	auto generateMD5Entries = cli.get<bool>(ARG_GEN_MD5_ENTRIES_LONG);
	auto preloadExtensions = cli.get<std::vector<std::string>>(ARG_PRELOAD_SHORT);
	auto saveToDir = cli.get<bool>(ARG_SINGLE_FILE_SHORT);
	auto fileTree = cli.get<bool>(ARG_FILE_TREE_LONG);
	auto signPath = cli.is_used(ARG_SIGN_SHORT) ? cli.get(ARG_SIGN_SHORT) : "";
	auto shouldVerify = cli.is_used(ARG_VERIFY_CHECKSUMS_LONG) || cli.is_used(ARG_VERIFY_SIGNATURE_LONG);

	std::unique_ptr<indicators::IndeterminateProgressBar> bar;
	if (!noProgressBar) {
		bar = std::make_unique<indicators::IndeterminateProgressBar>(
			indicators::option::BarWidth{40},
			indicators::option::Start{"["},
			indicators::option::Fill{"·"},
			indicators::option::Lead{"<==>"},
			indicators::option::End{"]"},
			indicators::option::PostfixText{"Packing files..."}
		);
	}

	auto vpk = VPK::createFromDirectoryProcedural(outputPath, inputPath, [saveToDir, &preloadExtensions, noProgressBar, &bar](const std::string& fullEntryPath) {
		int preloadBytes = 0;
		for (const auto& preloadExtension : preloadExtensions) {
			if ((std::count(preloadExtension.begin(), preloadExtension.end(), '.') > 0 && std::filesystem::path(fullEntryPath).extension().string().ends_with(preloadExtension)) ||
				std::filesystem::path(fullEntryPath).filename().string() == preloadExtension) {
				preloadBytes = VPK_MAX_PRELOAD_BYTES;
				break;
			}
		}
		if (!noProgressBar) {
			bar->tick();
		}
		return std::make_tuple(saveToDir, preloadBytes);
	}, {
		.vpk_version = version,
		.vpk_preferredChunkSize = preferredChunkSize,
		.vpk_generateMD5Entries = generateMD5Entries,
	});

	if (!noProgressBar) {
		bar->mark_as_completed();
	}

	if (fileTree) {
		::fileTree(outputPath);
	}
	if (!signPath.empty()) {
		::sign(cli, outputPath);
	}
	if (shouldVerify) {
		::verify(cli, outputPath);
	}

	std::cout << "Successfully created VPK at \"" << vpk->getFilepath() << "\"." << std::endl;
}

/// Generate private/public key files
void generateKeyPair(const std::string& inputPath) {
	if (!VPK::generateKeyPairFiles(inputPath)) {
		std::cerr << "Failed to generate public/private key files at \"" << inputPath << ".[private/public]key.vdf\"!" << std::endl;
		return;
	}
	std::cout << "Generated private/public key files at \"" << inputPath << ".[private/public]key.vdf\"." << std::endl;
	std::cout << "Remember to NEVER share a private key! The public key is fine to share." << std::endl;
}

} // namespace

int main(int argc, const char* const* argv) {
	argparse::ArgumentParser cli{std::string{PROJECT_NAME} + "cli", PROJECT_VERSION_PRETTY.data(), argparse::default_arguments::help};

#ifdef _WIN32
	// Add the Windows-specific ones because why not
	cli.set_prefix_chars("-/");
	cli.set_assign_chars("=:");
#endif

	cli.add_description("This program currently has five modes:\n"
	                    " - Pack:     Packs the contents of a given directory into a VPK.\n"
	                    " - Generate: Generates files related to VPK creation, such as a public/private keypair.\n"
	                    " - Preview:  Prints the file tree of an existing VPK to the console. Can also be combined\n"
	                    "             with Pack mode to print the file tree of the new VPK.\n"
	                    " - Sign:     Signs an existing VPK. Can also be combined with Pack mode to sign the new VPK.\n"
	                    " - Verify:   Verify an existing VPK's checksums and/or signature. If used together with\n"
	                    "             Pack or Sign modes, it will verify the VPK after the other modes are finished.\n"
	                    "Modes are automatically determined by the <path> argument, as well as the other given arguments\n"
	                    "when it is still unclear.");

	cli.add_argument("<path>")
		.help("(Pack)     The directory to pack into a VPK.\n"
		      "(Generate) The name of the file(s) to generate.\n"
		      "(Preview)  The path to the VPK to print the file tree of.\n"
		      "(Sign)     The path to the VPK to sign.\n"
		      "(Verify)   The path to the VPK to verify the contents of.")
		.required();

	cli.add_argument(ARG_OUTPUT_SHORT, ARG_OUTPUT_LONG)
		.help("The path to the output VPK or directory. If unspecified, will default next to the input.");

	cli.add_argument(ARG_NO_PROGRESS_LONG)
		.help("Hide all progress bars.")
		.flag();

	cli.add_argument(ARG_VERSION_SHORT, ARG_VERSION_LONG)
		.help("(Pack) The version of the VPK. Can be 1 or 2.")
		.default_value("2")
		.choices("1", "2")
		.nargs(1);

	cli.add_argument(ARG_CHUNKSIZE_SHORT, ARG_CHUNKSIZE_LONG)
		.help("(Pack) The size of each archive in mb.")
		.default_value("200")
		.nargs(1);

	cli.add_argument(ARG_GEN_MD5_ENTRIES_LONG)
		.help("(Pack) Generate MD5 hashes for each file (v2 only).")
		.flag();

	cli.add_argument(ARG_PRELOAD_SHORT, ARG_PRELOAD_LONG)
		.help("(Pack) If a file's extension is in this list, the first kilobyte will be\n"
		      "preloaded in the directory VPK. Full file names are also supported here\n"
		      "(i.e. this would preload any files named README.md or files ending in vmt:\n"
		      "\"-p README.md vmt\"). It preloads materials by default to match Valve behavior.")
		.default_value(std::vector<std::string>{"vmt"})
		.remaining();

	cli.add_argument(ARG_SINGLE_FILE_SHORT, ARG_SINGLE_FILE_LONG)
		.help("(Pack) Pack all files into the directory VPK (single-file build).\n"
		      "Breaks the VPK if its size will be >= 4gb!")
		.flag();

	cli.add_argument(ARG_SIGN_SHORT, ARG_SIGN_LONG)
		.help("(Pack) Sign the output VPK with the key in the given private key file (v2 only).\n"
		      "(Sign) Sign the VPK with the key in the given private key file (v2 only).");

	cli.add_argument(ARG_GEN_KEYPAIR_LONG)
		.help("(Generate) Generate files containing public/private keys with the specified name.\n"
		      "DO NOT SHARE THE PRIVATE KEY FILE WITH ANYONE! Move it to a safe place where it\n"
		      "will not be shipped.")
		.flag();

	cli.add_argument(ARG_FILE_TREE_LONG)
		.help("(Preview) Prints the file tree of the given VPK to the console.")
		.flag();

	cli.add_argument(ARG_VERIFY_CHECKSUMS_LONG)
		.help(R"((Verify) Verify the VPK's checksums. Can be "files", "vpk", or "all" (without quotes).)")
		.choices("files", "vpk", "all")
		.nargs(1);

	cli.add_argument(ARG_VERIFY_SIGNATURE_LONG)
		.help("(Verify) Verify the VPK's signature if it exists.")
		.flag();

	cli.add_epilog(R"(Program details:                                               )"        "\n"
	               R"(                    /$$                       /$$ /$$   /$$    )"        "\n"
	               R"(                   | $$                      | $$|__/  | $$    )"        "\n"
	               R"( /$$    /$$/$$$$$$ | $$   /$$  /$$$$$$   /$$$$$$$ /$$ /$$$$$$  )"        "\n"
	               R"(|  $$  /$$/$$__  $$| $$  /$$/ /$$__  $$ /$$__  $$| $$|_  $$_/  )"        "\n"
	               R"( \  $$/$$/ $$  \ $$| $$$$$$/ | $$$$$$$$| $$  | $$| $$  | $$    )"        "\n"
	               R"(  \  $$$/| $$  | $$| $$_  $$ | $$_____/| $$  | $$| $$  | $$ /$$)"        "\n"
	               R"(   \  $/ | $$$$$$$/| $$ \  $$|  $$$$$$$|  $$$$$$$| $$  |  $$$$/)"        "\n"
	               R"(    \_/  | $$____/ |__/  \__/ \_______/ \_______/|__/   \____/ )"        "\n"
	               R"(         | $$                                                  )"        "\n"
	               R"(         | $$             version v)"s + PROJECT_VERSION_PRETTY.data() + "\n"
	               R"(         |__/                                                  )"        "\n"
	               R"(                                                               )"        "\n"
	               "Created by craftablescience. Contributors and libraries used are"          "\n"
	               "listed in CREDITS.md. " + PROJECT_NAME_PRETTY.data() + " is licensed under the MIT License.");

	try {
		cli.parse_args(argc, argv);

		std::string inputPath{cli.get("<path>")};
		if (inputPath.ends_with('/') || inputPath.ends_with('\\')) {
			inputPath.pop_back();
		}

		if (std::filesystem::exists(inputPath)) {
			if (std::filesystem::status(inputPath).type() == std::filesystem::file_type::directory) {
				::pack(cli, inputPath);
			} else {
				bool foundAction = false;
				if (cli.is_used(ARG_FILE_TREE_LONG)) {
					foundAction = true;
					::fileTree(inputPath);
				}
				if (cli.is_used(ARG_SIGN_SHORT)) {
					foundAction = true;
					::sign(cli, inputPath);
				}
				if (cli.is_used(ARG_VERIFY_CHECKSUMS_LONG) || cli.is_used(ARG_VERIFY_SIGNATURE_LONG)) {
					foundAction = true;
					::verify(cli, inputPath);
				}
				if (!foundAction) {
					throw std::runtime_error{"No action taken! Add some arguments to clarify your intent."};
				}
			}
		} else if (cli.get<bool>(ARG_GEN_KEYPAIR_LONG)) {
			::generateKeyPair(inputPath);
		} else {
			throw std::runtime_error("Given path does not exist!");
		}
	} catch (const std::exception& e) {
		if (argc > 1) {
			std::cerr << e.what() << '\n' << std::endl;
			std::cerr << cli << std::endl;
		} else {
			std::cout << cli << std::endl;
		}
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
