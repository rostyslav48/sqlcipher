/*
** SQLCipher
** http://sqlcipher.net
**
** Copyright (c) 2008 - 2013, ZETETIC LLC
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the ZETETIC LLC nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY ZETETIC LLC ''AS IS'' AND ANY
** EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL ZETETIC LLC BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*/
/* BEGIN SQLCIPHER */
#ifdef SQLITE_HAS_CODEC
#ifdef SQLCIPHER_CRYPTO_CC
#include "sqlcipher.h"
#include <CommonCrypto/CommonCrypto.h>
#include <Security/SecRandom.h>
#include <CoreFoundation/CoreFoundation.h>

int sqlcipher_cc_setup(sqlcipher_provider *p);

static int sqlcipher_cc_add_random(void *ctx, const void *buffer, int length) {
  return SQLITE_OK;
}

/* generate a defined number of random bytes */
static int sqlcipher_cc_random (void *ctx, void *buffer, int length) {
  return (SecRandomCopyBytes(kSecRandomDefault, length, (uint8_t *)buffer) == kCCSuccess) ? SQLITE_OK : SQLITE_ERROR;
}

static const char* sqlcipher_cc_get_provider_name(void *ctx) {
  return "commoncrypto";
}

static const char* sqlcipher_cc_get_provider_version(void *ctx) {
#if TARGET_OS_MAC
  CFTypeRef version;
  CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.apple.security"));
  if(bundle == NULL) {
    return "unknown";
  }
  version = CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFBundleShortVersionString"));
  return CFStringGetCStringPtr(version, kCFStringEncodingUTF8);
#else
  return "unknown";
#endif
}

static int sqlcipher_cc_hmac(
  void *ctx, int algorithm,
  const unsigned char *hmac_key, int key_sz,
  const unsigned char *in, int in_sz,
  const unsigned char *in2, int in2_sz,
  unsigned char *out
) {
  CCHmacContext hmac_context;
  if(in == NULL) return SQLITE_ERROR;
  switch(algorithm) {
    case SQLCIPHER_HMAC_SHA1:
      CCHmacInit(&hmac_context, kCCHmacAlgSHA1, hmac_key, key_sz);
      break;
    case SQLCIPHER_HMAC_SHA256:
      CCHmacInit(&hmac_context, kCCHmacAlgSHA256, hmac_key, key_sz);
      break;
    case SQLCIPHER_HMAC_SHA512:
      CCHmacInit(&hmac_context, kCCHmacAlgSHA512, hmac_key, key_sz);
      break;
    default:
      return SQLITE_ERROR;
  }
  CCHmacUpdate(&hmac_context, in, in_sz);
  if(in2 != NULL) CCHmacUpdate(&hmac_context, in2, in2_sz);
  CCHmacFinal(&hmac_context, out);
  return SQLITE_OK; 
}

static int sqlcipher_cc_kdf(
  void *ctx, int algorithm,
  const unsigned char *pass, int pass_sz,
  const unsigned char* salt, int salt_sz,
  int workfactor,
  int key_sz, unsigned char *key
) {
  switch(algorithm) {
    case SQLCIPHER_HMAC_SHA1:
      if(CCKeyDerivationPBKDF(kCCPBKDF2, (const char *)pass, pass_sz, salt, salt_sz, kCCPRFHmacAlgSHA1, workfactor, key, key_sz) != kCCSuccess) return SQLITE_ERROR;
      break;
    case SQLCIPHER_HMAC_SHA256:
      if(CCKeyDerivationPBKDF(kCCPBKDF2, (const char *)pass, pass_sz, salt, salt_sz, kCCPRFHmacAlgSHA256, workfactor, key, key_sz) != kCCSuccess) return SQLITE_ERROR;
      break;
    case SQLCIPHER_HMAC_SHA512:
      if(CCKeyDerivationPBKDF(kCCPBKDF2, (const char *)pass, pass_sz, salt, salt_sz, kCCPRFHmacAlgSHA512, workfactor, key, key_sz) != kCCSuccess) return SQLITE_ERROR;
      break;
    default:
      return SQLITE_ERROR;
  }
  return SQLITE_OK; 
}

static int sqlcipher_cc_cipher(
  void *ctx, int mode,
  const unsigned char *key, int key_sz,
  const unsigned char *iv,
  const unsigned char *in, int in_sz,
  unsigned char *out
) {
  CCCryptorRef cryptor;
  size_t tmp_csz, csz;
  CCOperation op = mode == CIPHER_ENCRYPT ? kCCEncrypt : kCCDecrypt;

  if(CCCryptorCreate(op, kCCAlgorithmAES128, 0, key, kCCKeySizeAES256, iv, &cryptor) != kCCSuccess) return SQLITE_ERROR;
  if(CCCryptorUpdate(cryptor, in, in_sz, out, in_sz, &tmp_csz) != kCCSuccess) return SQLITE_ERROR;
  csz = tmp_csz;
  out += tmp_csz;
  if(CCCryptorFinal(cryptor, out, in_sz - csz, &tmp_csz) != kCCSuccess) return SQLITE_ERROR;
  csz += tmp_csz;
  if(CCCryptorRelease(cryptor) != kCCSuccess) return SQLITE_ERROR;
  assert(in_sz == csz);

  return SQLITE_OK; 
}

static const char* sqlcipher_cc_get_cipher(void *ctx) {
  return "aes-256-cbc";
}

static int sqlcipher_cc_get_key_sz(void *ctx) {
  return kCCKeySizeAES256;
}

static int sqlcipher_cc_get_iv_sz(void *ctx) {
  return kCCBlockSizeAES128;
}

static int sqlcipher_cc_get_block_sz(void *ctx) {
  return kCCBlockSizeAES128;
}

static int sqlcipher_cc_get_hmac_sz(void *ctx, int algorithm) {
  switch(algorithm) {
    case SQLCIPHER_HMAC_SHA1:
      return CC_SHA1_DIGEST_LENGTH;
      break;
    case SQLCIPHER_HMAC_SHA256:
      return CC_SHA256_DIGEST_LENGTH;
      break;
    case SQLCIPHER_HMAC_SHA512:
      return CC_SHA512_DIGEST_LENGTH;
      break;
    default:
      return 0;
  }
}

static int sqlcipher_cc_ctx_init(void **ctx) {
  return SQLITE_OK;
}

static int sqlcipher_cc_ctx_free(void **ctx) {
  return SQLITE_OK;
}

static int sqlcipher_cc_fips_status(void *ctx) {
  return 0;
}

int sqlcipher_cc_setup(sqlcipher_provider *p) {
  p->init = NULL;
  p->shutdown = NULL;
  p->random = sqlcipher_cc_random;
  p->get_provider_name = sqlcipher_cc_get_provider_name;
  p->hmac = sqlcipher_cc_hmac;
  p->kdf = sqlcipher_cc_kdf;
  p->cipher = sqlcipher_cc_cipher;
  p->get_cipher = sqlcipher_cc_get_cipher;
  p->get_key_sz = sqlcipher_cc_get_key_sz;
  p->get_iv_sz = sqlcipher_cc_get_iv_sz;
  p->get_block_sz = sqlcipher_cc_get_block_sz;
  p->get_hmac_sz = sqlcipher_cc_get_hmac_sz;
  p->ctx_init = sqlcipher_cc_ctx_init;
  p->ctx_free = sqlcipher_cc_ctx_free;
  p->add_random = sqlcipher_cc_add_random;
  p->fips_status = sqlcipher_cc_fips_status;
  p->get_provider_version = sqlcipher_cc_get_provider_version;
  return SQLITE_OK;
}

#endif
#endif
/* END SQLCIPHER */
