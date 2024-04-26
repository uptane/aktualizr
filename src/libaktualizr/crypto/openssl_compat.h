#ifndef AKTUALIZR_OPENSSL_COMPAT_H
#define AKTUALIZR_OPENSSL_COMPAT_H

#include <openssl/opensslv.h>

#ifndef OPENSSL_VERSION_NUMBER
#error "OPENSSL_VERSION_NUMBER is not defined"
#endif

#if (OPENSSL_VERSION_NUMBER < 0x10100000)
#error "openssl releases before 1.1 are not supported"
#endif

#define AKTUALIZR_OPENSSL_PRE_3 (OPENSSL_VERSION_NUMBER < 0x30000000)
#endif  // AKTUALIZR_OPENSSL_COMPAT_H
