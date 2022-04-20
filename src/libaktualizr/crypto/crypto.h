#ifndef CRYPTO_H_
#define CRYPTO_H_

#include <openssl/ossl_typ.h>           // for X509, BIO, ENGINE, EVP_PKEY
#include <sodium/crypto_hash_sha256.h>  // for crypto_hash_sha256_init, cryp...
#include <sodium/crypto_hash_sha512.h>  // for crypto_hash_sha512_init, cryp...

#include <algorithm>  // for copy
#include <array>      // for array
#include <cstdint>    // for uint64_t
#include <memory>     // for shared_ptr
#include <string>     // for string

#include "libaktualizr/types.h"  // for Hash, KeyType, Hash::Type
#include "utilities/utils.h"     // for StructGuard

#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

// some older versions of openssl have BIO_new_mem_buf defined with first parameter of type (void*)
//   which is not true and breaks our build
#undef BIO_new_mem_buf
BIO *BIO_new_mem_buf(const void *, int);  // NOLINT(readability-redundant-declaration)

class MultiPartHasher {
 public:
  MultiPartHasher() = default;
  virtual ~MultiPartHasher() = default;
  MultiPartHasher(const MultiPartHasher &) = delete;
  MultiPartHasher(MultiPartHasher &&) = delete;
  MultiPartHasher &operator=(const MultiPartHasher &) = delete;
  MultiPartHasher &operator=(MultiPartHasher &&) = delete;

  using Ptr = std::shared_ptr<MultiPartHasher>;
  static Ptr create(Hash::Type hash_type);

  virtual void update(const unsigned char *part, uint64_t size) = 0;
  virtual void reset() = 0;
  virtual std::string getHexDigest() = 0;
  virtual Hash getHash() = 0;
};

class MultiPartSHA512Hasher : public MultiPartHasher {
 public:
  MultiPartSHA512Hasher() { crypto_hash_sha512_init(&state_); }
  ~MultiPartSHA512Hasher() override = default;
  MultiPartSHA512Hasher(const MultiPartSHA512Hasher &) = delete;
  MultiPartSHA512Hasher(MultiPartSHA512Hasher &&) = delete;
  MultiPartSHA512Hasher &operator=(const MultiPartSHA512Hasher &) = delete;
  MultiPartSHA512Hasher &operator=(MultiPartSHA512Hasher &&) = delete;
  void update(const unsigned char *part, uint64_t size) override { crypto_hash_sha512_update(&state_, part, size); }
  void reset() override { crypto_hash_sha512_init(&state_); }
  std::string getHexDigest() override;
  Hash getHash() override { return Hash(Hash::Type::kSha512, getHexDigest()); }

 private:
  crypto_hash_sha512_state state_{};
};

class MultiPartSHA256Hasher : public MultiPartHasher {
 public:
  MultiPartSHA256Hasher() { crypto_hash_sha256_init(&state_); }
  ~MultiPartSHA256Hasher() override = default;
  MultiPartSHA256Hasher(const MultiPartSHA256Hasher &) = delete;
  MultiPartSHA256Hasher(MultiPartSHA256Hasher &&) = delete;
  MultiPartSHA256Hasher &operator=(const MultiPartSHA256Hasher &) = delete;
  MultiPartSHA256Hasher &operator=(MultiPartSHA256Hasher &&) = delete;
  void update(const unsigned char *part, uint64_t size) override { crypto_hash_sha256_update(&state_, part, size); }
  void reset() override { crypto_hash_sha256_init(&state_); }
  std::string getHexDigest() override;

  Hash getHash() override { return Hash(Hash::Type::kSha256, getHexDigest()); }

 private:
  crypto_hash_sha256_state state_{};
};

class Crypto {
 public:
  static std::string sha256digest(const std::string &text);
  /** A lower case, hexadecimal version of sha256digest */
  static std::string sha256digestHex(const std::string &text);
  static std::string sha512digest(const std::string &text);
  /** A lower case, hexadecimal version of sha512digest */
  static std::string sha512digestHex(const std::string &text);
  static std::string RSAPSSSign(ENGINE *engine, const std::string &private_key, const std::string &message);
  static std::string Sign(KeyType key_type, ENGINE *engine, const std::string &private_key, const std::string &message);
  static std::string ED25519Sign(const std::string &private_key, const std::string &message);
  static bool parseP12(BIO *p12_bio, const std::string &p12_password, std::string *out_pkey, std::string *out_cert,
                       std::string *out_ca);
  static std::string extractSubjectCN(const std::string &cert);
  static StructGuard<EVP_PKEY> generateRSAKeyPairEVP(KeyType key_type);
  static StructGuard<EVP_PKEY> generateRSAKeyPairEVP(int bits);
  static bool generateRSAKeyPair(KeyType key_type, std::string *public_key, std::string *private_key);
  static bool generateEDKeyPair(std::string *public_key, std::string *private_key);
  static bool generateKeyPair(KeyType key_type, std::string *public_key, std::string *private_key);

  static bool RSAPSSVerify(const std::string &public_key, const std::string &signature, const std::string &message);
  static bool ED25519Verify(const std::string &public_key, const std::string &signature, const std::string &message);

  static bool IsRsaKeyType(KeyType type);
  static KeyType IdentifyRSAKeyType(const std::string &public_key_pem);

  static StructGuard<X509> generateCert(int rsa_bits, int cert_days, const std::string &cert_c,
                                        const std::string &cert_st, const std::string &cert_o,
                                        const std::string &cert_cn, bool self_sign = false);
  static void signCert(const std::string &cacert_path, const std::string &capkey_path, X509 *certificate);
  static void serializeCert(std::string *pkey, std::string *cert, X509 *certificate);
};

#endif  // CRYPTO_H_
