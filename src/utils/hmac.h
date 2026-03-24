#pragma once

#include <string>
#include <cstring>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace bybit {

// S1: Zero-allocation HMAC-SHA256 hex encoding using fixed buffer + lookup table
inline std::string hmac_sha256(const std::string& key, const std::string& data) {
    static constexpr char HEX_LUT[] = "0123456789abcdef";

    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         result, &len);

    // SHA256 produces 32 bytes → 64 hex chars + null
    char hex[65];
    for (unsigned int i = 0; i < len; ++i) {
        hex[i * 2]     = HEX_LUT[result[i] >> 4];
        hex[i * 2 + 1] = HEX_LUT[result[i] & 0x0F];
    }
    hex[len * 2] = '\0';
    return std::string(hex, len * 2);
}

} // namespace bybit
