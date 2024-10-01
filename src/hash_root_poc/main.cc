#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>               // Added to fix the compilation error
#include <openssl/sha.h>
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

// Function to hash the contents of a file
std::string hash_file(const fs::path& file_path) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    std::ifstream file(file_path.string(), std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << file_path << "\n";
        return "";
    }

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        SHA256_Update(&sha256, buffer, file.gcount());
    }
    // Handle any remaining bytes
    SHA256_Update(&sha256, buffer, file.gcount());

    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return ss.str();
}

// Function to hash the entire filesystem by combining file hashes
std::string hash_filesystem_root(const fs::path& root_path) {
    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    // Traverse the filesystem starting from root
    for (fs::recursive_directory_iterator iter(root_path), end; iter != end; ++iter) {
        if (fs::is_regular_file(iter->path())) {
            std::string file_hash = hash_file(iter->path());
            // Combine file hash and file path into the overall hash
            SHA256_Update(&sha256, file_hash.c_str(), file_hash.size());
            std::string file_path_str = iter->path().string();
            SHA256_Update(&sha256, file_path_str.c_str(), file_path_str.size());
        }
    }

    // Finalize the overall filesystem hash
    unsigned char final_hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(final_hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)final_hash[i];
    }

    return ss.str();
}

int main() {
    fs::path root_path = "/";  // Root of the filesystem

    std::cout << "Hashing the filesystem at root: " << root_path << "\n";
    std::string fs_hash = hash_filesystem_root(root_path);
    std::cout << "Filesystem Hash: " << fs_hash << "\n";

    return 0;
}
