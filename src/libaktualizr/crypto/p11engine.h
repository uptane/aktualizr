#ifndef P11ENGINE_H_
#define P11ENGINE_H_

#include <memory>

#include "libaktualizr/config.h"

#include <openssl/engine.h>
#include <openssl/err.h>
#include "gtest/gtest_prod.h"

#include "logging/logging.h"

class P11ContextWrapper {
 public:
  explicit P11ContextWrapper(const boost::filesystem::path &module);
  ~P11ContextWrapper();  // NOLINT(performance-trivially-destructible)
  P11ContextWrapper(const P11ContextWrapper &) = delete;
  P11ContextWrapper(P11ContextWrapper &&) = delete;
  P11ContextWrapper &operator=(const P11ContextWrapper &) = delete;
  P11ContextWrapper &operator=(P11ContextWrapper &&) = delete;
  PKCS11_ctx_st *get() const { return ctx; }

 private:
  PKCS11_ctx_st *ctx;
};

class P11SlotsWrapper {
 public:
  explicit P11SlotsWrapper(PKCS11_ctx_st *ctx_in);
  ~P11SlotsWrapper();  // NOLINT(performance-trivially-destructible)
  P11SlotsWrapper(const P11SlotsWrapper &) = delete;
  P11SlotsWrapper(P11SlotsWrapper &&) = delete;
  P11SlotsWrapper &operator=(const P11SlotsWrapper &) = delete;
  P11SlotsWrapper &operator=(P11SlotsWrapper &&) = delete;
  PKCS11_slot_st *get_slots() const { return slots_; }
  unsigned int get_nslots() const { return nslots; }

 private:
  PKCS11_ctx_st *ctx;
  PKCS11_slot_st *slots_;
  unsigned int nslots;
};

class P11EngineGuard;

class P11Engine {
 public:
  P11Engine(const P11Engine &) = delete;
  P11Engine(P11Engine &&) = delete;
  P11Engine &operator=(const P11Engine &) = delete;
  P11Engine &operator=(P11Engine &&) = delete;

  virtual ~P11Engine();

  ENGINE *getEngine() { return ssl_engine_; }
  std::string getItemFullId(const std::string &id) const { return uri_prefix_ + id; }
  bool readUptanePublicKey(const std::string &uptane_key_id, std::string *key_out);
  bool readTlsCert(const std::string &id, std::string *cert_out) const;
  bool generateUptaneKeyPair(const std::string &uptane_key_id);

 private:
  const boost::filesystem::path module_path_;
  const std::string pass_;
  ENGINE *ssl_engine_{nullptr};
  std::string uri_prefix_;
  P11ContextWrapper ctx_;
  P11SlotsWrapper wslots_;

  static boost::filesystem::path findPkcsLibrary();
  PKCS11_slot_st *findTokenSlot() const;

  explicit P11Engine(boost::filesystem::path module_path, std::string pass);

  friend class P11EngineGuard;
  FRIEND_TEST(crypto, findPkcsLibrary);
};

class P11EngineGuard {
 public:
  explicit P11EngineGuard(boost::filesystem::path module_path, std::string pass) {
    if (instance == nullptr) {
      instance = new P11Engine(std::move(module_path), std::move(pass));
    }
    ++ref_counter;
  }

  ~P11EngineGuard() {
    if (ref_counter != 0) {
      --ref_counter;
    }
    if (ref_counter == 0) {
      delete instance;
      instance = nullptr;
    }
  }

  P11EngineGuard(const P11EngineGuard &) = delete;
  P11EngineGuard(P11EngineGuard &&) = delete;
  P11EngineGuard &operator=(const P11EngineGuard &) = delete;
  P11EngineGuard &operator=(P11EngineGuard &&) = delete;

  P11Engine *operator->() const { return instance; }

 private:
  static P11Engine *instance;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
  static int ref_counter;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};

#endif
