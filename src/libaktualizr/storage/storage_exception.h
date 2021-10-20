#ifndef STORAGE_EXCEPTION_H_
#define STORAGE_EXCEPTION_H_

#include <stdexcept>
#include <string>

class StorageException : public std::runtime_error {
 public:
  explicit StorageException(const std::string& what) : std::runtime_error(what) {}
};

#endif  // STORAGE_EXCEPTION_H_
