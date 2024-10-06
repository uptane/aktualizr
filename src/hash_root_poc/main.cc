// File: system_hash.cpp

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <boost/filesystem.hpp>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <thread>
#include <mutex>
#include <atomic>

// // Namespace aliases for convenience
// namespace fs = boost::filesystem;

// // Function to compute SHA256 hash of a file using OpenSSL EVP interface
// std::string compute_file_sha256(const fs::path& filepath) {
//     std::ifstream file(filepath.string(), std::ios::binary);
//     if (!file) {
//         std::cerr << "Error opening file: " << filepath.string() << std::endl;
//         return "";
//     }

//     // Initialize OpenSSL EVP context
//     EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
//     if (mdctx == nullptr) {
//         std::cerr << "EVP_MD_CTX_new failed for file: " << filepath.string() << std::endl;
//         return "";
//     }

//     if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
//         std::cerr << "EVP_DigestInit_ex failed for file: " << filepath.string() << std::endl;
//         EVP_MD_CTX_free(mdctx);
//         return "";
//     }

//     const std::size_t buffer_size = 1 << 16; // 64KB buffer
//     std::vector<char> buffer(buffer_size);
//     while (file.read(buffer.data(), buffer_size)) {
//         if (EVP_DigestUpdate(mdctx, buffer.data(), file.gcount()) != 1) {
//             std::cerr << "EVP_DigestUpdate failed for file: " << filepath.string() << std::endl;
//             EVP_MD_CTX_free(mdctx);
//             return "";
//         }
//     }
//     // Handle the last partial read
//     if (file.gcount() > 0) {
//         if (EVP_DigestUpdate(mdctx, buffer.data(), file.gcount()) != 1) {
//             std::cerr << "EVP_DigestUpdate failed for file: " << filepath.string() << std::endl;
//             EVP_MD_CTX_free(mdctx);
//             return "";
//         }
//     }

//     unsigned char hash[EVP_MAX_MD_SIZE];
//     unsigned int length_of_hash = 0;

//     if (EVP_DigestFinal_ex(mdctx, hash, &length_of_hash) != 1) {
//         std::cerr << "EVP_DigestFinal_ex failed for file: " << filepath.string() << std::endl;
//         EVP_MD_CTX_free(mdctx);
//         return "";
//     }

//     EVP_MD_CTX_free(mdctx);

//     // Convert hash to hex string
//     std::string hash_hex;
//     hash_hex.reserve(length_of_hash * 2);
//     for (unsigned int i = 0; i < length_of_hash; ++i) {
//         char buf[3];
//         snprintf(buf, sizeof(buf), "%02x", hash[i]);
//         hash_hex += buf;
//     }

//     return hash_hex;
// }

// // Function to collect all regular file paths, excluding specified directories
// std::vector<fs::path> collect_all_files(const fs::path& root, const std::vector<fs::path>& exclude_dirs) {
//     std::vector<fs::path> all_files;

//     try {
//         fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied);
//         fs::recursive_directory_iterator endit;

//         while (it != endit) {
//             const fs::path& path = it->path();

//             // Check if current path is in the exclusion list
//             bool excluded = false;
//             for (const auto& excl : exclude_dirs) {
//                 if (path == excl || std::mismatch(excl.begin(), excl.end(), path.begin()).first == excl.end()) {
//                     excluded = true;
//                     if (fs::is_directory(*it)) {
//                         it.no_push(); // Do not recurse into this directory
//                     }
//                     break;
//                 }
//             }
//             if (excluded) {
//                 ++it;
//                 continue;
//             }

//             // If it's a regular file, add to the list
//             if (fs::is_regular_file(*it)) {
//                 all_files.push_back(path);
//             }

//             ++it;
//         }
//     }
//     catch (const fs::filesystem_error& e) {
//         std::cerr << "Filesystem error: " << e.what() << std::endl;
//     }

//     return all_files;
// }

// // Mutex to protect the file_hashes vector
// std::mutex hash_mutex;

// // Function to hash a subset of files
// void hash_worker(const std::vector<fs::path>& files, std::vector<std::string>& file_hashes, std::atomic<size_t>& processed_files, size_t total_files) {
//     for (const auto& filepath : files) {
//         std::string hash = compute_file_sha256(filepath);
//         if (!hash.empty()) {
//             std::lock_guard<std::mutex> guard(hash_mutex);
//             file_hashes.push_back(hash);
//         }
//         ++processed_files;
//         if (processed_files % 1000 == 0) {
//             std::cout << "Processed " << processed_files << " / " << total_files << " files.\r" << std::flush;
//         }
//     }
// }

int main() {
    // Initialize OpenSSL algorithms (optional in recent versions but good practice)
    // OpenSSL_add_all_algorithms();
    // ERR_load_BIO_strings();
    // ERR_load_crypto_strings();

    // // Define the root directory
    // fs::path root_dir = "/";

    // // Define directories to exclude
    // std::vector<fs::path> exclude_dirs = {
    //     "/proc",
    //     "/sys",
    //     "/dev",
    //     "/run",
    //     "/mnt",
    //     "/media",
    //     "/tmp",
    //     "/var/cache",
    //     "/var/run",
    //     "/var/tmp",
    //     "/home" // Optional: Exclude user data
    // };

    // std::cout << "Starting filesystem traversal..." << std::endl;

    // // Collect all file paths
    // std::vector<fs::path> all_files = collect_all_files(root_dir, exclude_dirs);

    // size_t total_files = all_files.size();
    // std::cout << "Total files to hash: " << total_files << std::endl;

    // if (total_files == 0) {
    //     std::cerr << "No files to process. Exiting." << std::endl;
    //     return 1;
    // }

    // // Determine the number of threads
    // unsigned int num_threads = std::thread::hardware_concurrency();
    // if (num_threads == 0) num_threads = 4; // Default to 4 if unable to detect
    // std::cout << "Using " << num_threads << " threads for hashing." << std::endl;

    // // Split files among threads
    // std::vector<std::vector<fs::path>> file_chunks(num_threads);
    // for (size_t i = 0; i < all_files.size(); ++i) {
    //     file_chunks[i % num_threads].push_back(all_files[i]);
    // }

    // // Vector to store all file hashes
    // std::vector<std::string> file_hashes;
    // file_hashes.reserve(total_files); // Reserve space to improve performance

    // // Atomic counter for progress reporting
    // std::atomic<size_t> processed_files(0);

    // // Launch threads
    // std::vector<std::thread> threads;
    // for (unsigned int i = 0; i < num_threads; ++i) {
    //     threads.emplace_back(hash_worker, std::cref(file_chunks[i]), std::ref(file_hashes), std::ref(processed_files), total_files);
    // }

    // // Join threads
    // for (auto& t : threads) {
    //     t.join();
    // }

    // std::cout << "\nAll files hashed. Sorting hashes..." << std::endl;

    // // Sort the hashes to ensure consistent ordering
    // std::sort(file_hashes.begin(), file_hashes.end());

    // std::cout << "Hashes sorted. Aggregating and computing root hash..." << std::endl;

    // // Concatenate all hashes into a single string
    // std::string concatenated_hashes;
    // concatenated_hashes.reserve(file_hashes.size() * 64); // Each SHA256 hash is 64 hex characters
    // for (const auto& hash : file_hashes) {
    //     concatenated_hashes += hash;
    // }

    // // Compute the root hash using EVP interface
    // EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    // if (mdctx == nullptr) {
    //     std::cerr << "EVP_MD_CTX_new failed for root hash computation." << std::endl;
    //     return 1;
    // }

    // if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
    //     std::cerr << "EVP_DigestInit_ex failed for root hash computation." << std::endl;
    //     EVP_MD_CTX_free(mdctx);
    //     return 1;
    // }

    // if (EVP_DigestUpdate(mdctx, concatenated_hashes.c_str(), concatenated_hashes.size()) != 1) {
    //     std::cerr << "EVP_DigestUpdate failed for root hash computation." << std::endl;
    //     EVP_MD_CTX_free(mdctx);
    //     return 1;
    // }

    // unsigned char root_hash_binary[EVP_MAX_MD_SIZE];
    // unsigned int length_of_hash = 0;

    // if (EVP_DigestFinal_ex(mdctx, root_hash_binary, &length_of_hash) != 1) {
    //     std::cerr << "EVP_DigestFinal_ex failed for root hash computation." << std::endl;
    //     EVP_MD_CTX_free(mdctx);
    //     return 1;
    // }

    // EVP_MD_CTX_free(mdctx);

    // // Convert root hash to hex string
    // std::string root_hash_hex;
    // root_hash_hex.reserve(length_of_hash * 2);
    // for (unsigned int i = 0; i < length_of_hash; ++i) {
    //     char buf[3];
    //     snprintf(buf, sizeof(buf), "%02x", root_hash_binary[i]);
    //     root_hash_hex += buf;
    // }

    // // Output the root hash
    // std::cout << "System Root Hash: " << root_hash_hex << std::endl;

    // // Optionally, save the hash to a file
    // std::ofstream outfile("/root/system_root_hash.txt");
    // if (outfile) {
    //     outfile << root_hash_hex << std::endl;
    //     outfile.close();
    //     std::cout << "Root hash saved to /root/system_root_hash.txt" << std::endl;
    // }
    // else {
    //     std::cerr << "Failed to write root hash to file." << std::endl;
    // }

    // // Cleanup OpenSSL (optional in recent versions but good practice)
    // EVP_cleanup();
    // CRYPTO_cleanup_all_ex_data();
    // ERR_free_strings();

    return 0;
}
