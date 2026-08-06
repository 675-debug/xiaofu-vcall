#include "PasswordHasher.h"
#include <windows.h>
#include <wincrypt.h>
#include <iomanip>
#include <sstream>

std::string PasswordHasher::sha256Hex(const std::string& text) {
    // 密码加密函数：将明文转换为 SHA-256 十六进制字符串。
    HCRYPTPROV providerHandle = 0;
    HCRYPTHASH hashHandle = 0;
    BYTE hashBytes[32] = {0};
    DWORD hashLength = sizeof(hashBytes);

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
                                                    hashBytes, &hashLength, 0);
    CryptDestroyHash(hashHandle);
    CryptReleaseContext(providerHandle, 0);
    if (!hashSucceeded)
        return std::string();

    std::stringstream hexStream;
    for (DWORD index = 0; index < hashLength; ++index) {
        hexStream << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(hashBytes[index]);
    }
    return hexStream.str();
}
