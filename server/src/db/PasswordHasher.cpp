#include "PasswordHasher.h"
#include <array>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <openssl/sha.h>
#endif
#include <iomanip>
#include <sstream>

std::string PasswordHasher::sha256Hex(const std::string& text) {
    // 密码加密函数：将明文转换为 SHA-256 十六进制字符串。
#ifdef _WIN32
    HCRYPTPROV providerHandle = 0;
    HCRYPTHASH hashHandle = 0;
    std::array<unsigned char, 32> hashBytes{};
    DWORD hashLength = static_cast<DWORD>(hashBytes.size());

    if (!CryptAcquireContextW(&providerHandle, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return std::string();
    if (!CryptCreateHash(providerHandle, CALG_SHA_256, 0, 0, &hashHandle)) {
        CryptReleaseContext(providerHandle, 0);
        return std::string();
    }
    const bool hashSucceeded = CryptHashData(hashHandle,
                                              reinterpret_cast<const BYTE*>(text.data()),
                                              static_cast<DWORD>(text.size()), 0)
                               && CryptGetHashParam(hashHandle, HP_HASHVAL,
                                                    hashBytes.data(), &hashLength, 0);
    CryptDestroyHash(hashHandle);
    CryptReleaseContext(providerHandle, 0);
    if (!hashSucceeded)
        return std::string();
#else
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hashBytes{};
    if (!SHA256(reinterpret_cast<const unsigned char*>(text.data()), text.size(),
                hashBytes.data()))
        return std::string();
    const std::size_t hashLength = hashBytes.size();
#endif

    std::stringstream hexStream;
    for (std::size_t index = 0; index < hashLength; ++index) {
        hexStream << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(hashBytes[index]);
    }
    return hexStream.str();
}
