#include "ios_vault_import.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IOS

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <stdint.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
    const size_t TAR_BLOCK = 512;
    const uint64_t MAX_ENTRY_BYTES = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    const uint64_t MAX_PACKAGE_BYTES = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    const size_t MAX_PACKAGE_ENTRIES = 4096;

    bool Is_Regular_File(const std::string& path)
    {
        struct stat info;
        return lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    }

    bool Create_Directories(const std::string& path)
    {
        if (path.empty() || path[0] != '/') {
            return false;
        }

        std::string partial;
        for (size_t i = 0; i < path.size(); ++i) {
            partial.push_back(path[i]);
            if (path[i] != '/' || partial.size() == 1) {
                continue;
            }
            partial.resize(partial.size() - 1);
            if (!partial.empty() && mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) {
                return false;
            }
            partial.push_back('/');
        }
        return mkdir(path.c_str(), 0700) == 0 || errno == EEXIST;
    }

    bool Remove_Tree(const std::string& path)
    {
        struct stat info;
        if (lstat(path.c_str(), &info) != 0) {
            return errno == ENOENT;
        }
        if (!S_ISDIR(info.st_mode)) {
            return unlink(path.c_str()) == 0;
        }

        DIR* directory = opendir(path.c_str());
        if (directory == NULL) {
            return false;
        }
        bool ok = true;
        struct dirent* entry;
        while ((entry = readdir(directory)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (!Remove_Tree(path + "/" + entry->d_name)) {
                ok = false;
                break;
            }
        }
        closedir(directory);
        return ok && rmdir(path.c_str()) == 0;
    }

    bool All_Zero(const unsigned char* block)
    {
        for (size_t i = 0; i < TAR_BLOCK; ++i) {
            if (block[i] != 0) {
                return false;
            }
        }
        return true;
    }

    bool Parse_Octal(const unsigned char* bytes, size_t length, uint64_t& value)
    {
        value = 0;
        bool saw_digit = false;
        for (size_t i = 0; i < length; ++i) {
            const unsigned char byte = bytes[i];
            if (byte == 0 || byte == ' ') {
                continue;
            }
            if (byte < '0' || byte > '7') {
                return false;
            }
            saw_digit = true;
            if (value > (UINT64_MAX - (byte - '0')) / 8) {
                return false;
            }
            value = value * 8 + (byte - '0');
        }
        return saw_digit;
    }

    bool Valid_Header_Checksum(const unsigned char* header)
    {
        uint64_t expected = 0;
        if (!Parse_Octal(header + 148, 8, expected)) {
            return false;
        }
        uint64_t actual = 0;
        for (size_t i = 0; i < TAR_BLOCK; ++i) {
            actual += i >= 148 && i < 156 ? static_cast<unsigned char>(' ') : header[i];
        }
        return actual == expected;
    }

    bool Safe_Relative_Path(const std::string& path)
    {
        if (path.empty() || path.size() > 480 || path[0] == '/' || path.find('\\') != std::string::npos) {
            return false;
        }
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (part.empty() || part == "." || part == "..") {
                return false;
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
        return true;
    }

    bool Ensure_Parent_Directory(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos || Create_Directories(path.substr(0, slash));
    }

    std::string Program_Directory(const char* argv0)
    {
        if (argv0 == NULL) {
            return std::string();
        }
        std::string path(argv0);
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    std::string Find_Transfer_Package(const std::string& directory, const std::string& package_name)
    {
        const std::string exact = directory + "/" + package_name;
        if (Is_Regular_File(exact)) {
            return exact;
        }

        const std::string extension = ".vcvault";
        const size_t extension_at = package_name.rfind(extension);
        const std::string prefix = extension_at == std::string::npos
            ? package_name
            : package_name.substr(0, extension_at);
        DIR* entries = opendir(directory.c_str());
        if (entries == NULL) {
            return std::string();
        }
        std::string match;
        struct dirent* entry;
        while ((entry = readdir(entries)) != NULL) {
            const std::string name(entry->d_name);
            if (name.compare(0, prefix.size(), prefix) != 0 || name.size() < extension.size()
                || name.compare(name.size() - extension.size(), extension.size(), extension) != 0) {
                continue;
            }
            const std::string candidate = directory + "/" + name;
            if (Is_Regular_File(candidate)) {
                match = candidate;
                break;
            }
        }
        closedir(entries);
        return match;
    }

    bool Manifest_Matches_Game(const std::string& manifest_path, const std::string& game_id)
    {
        struct stat info;
        if (stat(manifest_path.c_str(), &info) != 0 || info.st_size <= 0 || info.st_size > 1024 * 1024) {
            return false;
        }
        FILE* manifest = fopen(manifest_path.c_str(), "rb");
        if (manifest == NULL) {
            return false;
        }
        std::string contents(static_cast<size_t>(info.st_size), '\0');
        const bool read_ok = fread(&contents[0], 1, contents.size(), manifest) == contents.size();
        fclose(manifest);
        return read_ok
            && contents.find("\"format\": \"vanilla-vault-transfer\"") != std::string::npos
            && contents.find("\"game\": \"" + game_id + "\"") != std::string::npos;
    }

    bool Extract_Transfer(const std::string& package_path,
                          const std::string& staging_path,
                          const std::string& game_id,
                          const std::string& required_file,
                          std::string& error)
    {
        FILE* source = fopen(package_path.c_str(), "rb");
        if (source == NULL) {
            error = "The iOS transfer package could not be opened.";
            return false;
        }
        if (!Remove_Tree(staging_path) || !Create_Directories(staging_path)) {
            fclose(source);
            error = "The app could not prepare its private game-data folder.";
            return false;
        }

        bool ok = true;
        bool saw_manifest = false;
        bool saw_end = false;
        uint64_t total_size = 0;
        size_t entry_count = 0;
        unsigned char header[TAR_BLOCK];
        unsigned char buffer[64 * 1024];
        while (fread(header, 1, TAR_BLOCK, source) == TAR_BLOCK) {
            if (All_Zero(header)) {
                saw_end = true;
                break;
            }
            if (!Valid_Header_Checksum(header)) {
                error = "The transfer package header failed its integrity check.";
                ok = false;
                break;
            }

            size_t name_length = 0;
            while (name_length < 100 && header[name_length] != 0) {
                ++name_length;
            }
            const std::string relative(reinterpret_cast<char*>(header), name_length);
            uint64_t size = 0;
            const unsigned char type = header[156];
            if (!Safe_Relative_Path(relative) || !Parse_Octal(header + 124, 12, size)
                || size > MAX_ENTRY_BYTES || (type != 0 && type != '0')) {
                error = "The transfer package contains an unsafe or unsupported entry.";
                ok = false;
                break;
            }
            ++entry_count;
            if (entry_count > MAX_PACKAGE_ENTRIES || total_size > MAX_PACKAGE_BYTES - size) {
                error = "The transfer package is larger than the app's safe import limit.";
                ok = false;
                break;
            }
            total_size += size;

            const std::string output_path = staging_path + "/" + relative;
            if (!Ensure_Parent_Directory(output_path)) {
                error = "The app could not create the imported game-data folders.";
                ok = false;
                break;
            }
            FILE* output = fopen(output_path.c_str(), "wb");
            if (output == NULL) {
                error = "The app could not write an imported game-data file.";
                ok = false;
                break;
            }

            uint64_t remaining = size;
            while (remaining > 0) {
                const size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : static_cast<size_t>(remaining);
                if (fread(buffer, 1, chunk, source) != chunk || fwrite(buffer, 1, chunk, output) != chunk) {
                    ok = false;
                    error = "The transfer package ended before all game data was imported.";
                    break;
                }
                remaining -= chunk;
            }
            if (fclose(output) != 0 && ok) {
                ok = false;
                error = "The imported game-data file could not be finalised.";
            }
            if (!ok) {
                break;
            }
            saw_manifest = saw_manifest || relative == "VANILLA-VAULT-MANIFEST.json";

            const uint64_t padding = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
            if (padding > 0 && fseeko(source, static_cast<off_t>(padding), SEEK_CUR) != 0) {
                error = "The transfer package has invalid file padding.";
                ok = false;
                break;
            }
        }
        fclose(source);

        if (ok && !saw_end) {
            error = "The transfer package is truncated.";
            ok = false;
        }
        if (ok && !saw_manifest) {
            error = "This is not a Vanilla Vault transfer package.";
            ok = false;
        }
        if (ok && !Manifest_Matches_Game(staging_path + "/VANILLA-VAULT-MANIFEST.json", game_id)) {
            error = "This transfer package is for the other Vanilla Conquer app.";
            ok = false;
        }
        if (ok && !Is_Regular_File(staging_path + "/" + required_file)) {
            error = "The package is missing the core game-data file required by this app.";
            ok = false;
        }
        if (!ok) {
            Remove_Tree(staging_path);
        }
        return ok;
    }
} // namespace

bool IOS_Vault_Import_And_Configure(const char* game_id,
                                    const char* package_name,
                                    const char* required_file,
                                    const char* argv0,
                                    std::string& error_message)
{
    error_message.clear();
    const char* home = getenv("HOME");
    if (home == NULL || home[0] != '/' || game_id == NULL || package_name == NULL || required_file == NULL) {
        error_message = "The iOS app sandbox could not be located.";
        return false;
    }

    const std::string documents = std::string(home) + "/Documents";
    const std::string vault_root = documents + "/VaultData";
    const std::string target = vault_root + "/" + game_id;
    const std::string required = target + "/" + required_file;
    std::string package_path = Find_Transfer_Package(documents, package_name);
    if (package_path.empty()) {
        package_path = Find_Transfer_Package(documents + "/Inbox", package_name);
    }
    if (package_path.empty()) {
        if (Is_Regular_File(required)) {
            setenv("VANILLA_DATA_PATH", target.c_str(), 1);
            return true;
        }
        const std::string bundled = Program_Directory(argv0) + "/" + required_file;
        if (Is_Regular_File(bundled)) {
            return true;
        }
        error_message = std::string("Download ") + package_name
            + " from your private Vanilla Vault, save it in this app's Files folder, then reopen the app.";
        return false;
    }

    if (!Create_Directories(vault_root)) {
        error_message = "The app could not create its private VaultData folder.";
        return false;
    }
    const std::string staging = vault_root + "/." + std::string(game_id) + "-importing";
    if (!Extract_Transfer(package_path, staging, game_id, required_file, error_message)) {
        return false;
    }

    if (!Remove_Tree(target) || rename(staging.c_str(), target.c_str()) != 0) {
        Remove_Tree(staging);
        error_message = "The imported data could not replace the previous private game-data folder.";
        return false;
    }
    unlink(package_path.c_str());
    setenv("VANILLA_DATA_PATH", target.c_str(), 1);
    return true;
}

#else

bool IOS_Vault_Import_And_Configure(const char*, const char*, const char*, const char*, std::string& error_message)
{
    error_message.clear();
    return true;
}

#endif
