#ifndef IOS_VAULT_IMPORT_H
#define IOS_VAULT_IMPORT_H

#include <string>

// On iOS, imports a private Vanilla Vault transfer package from the app's
// Files-visible Documents directory and points the engine at the imported data.
// Existing bundle-embedded data remains a supported fallback. Other platforms
// return true without changing the environment.
bool IOS_Vault_Import_And_Configure(const char* game_id,
                                    const char* package_name,
                                    const char* required_file,
                                    const char* argv0,
                                    std::string& error_message);

#endif
