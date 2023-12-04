// Copyright (c) 2014-2015 The ShadowCoin developers
// Copyright (c) 2017-2023 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#define ENABLE_BIP39_ENGLISH 1
#define ENABLE_BIP39_FRENCH 1
#define ENABLE_BIP39_JAPANESE 1
#define ENABLE_BIP39_SPANISH 1
#define ENABLE_BIP39_CHINESE_S 1
#define ENABLE_BIP39_CHINESE_T 1
#define ENABLE_BIP39_ITALIAN 1
#define ENABLE_BIP39_KOREAN 1
#define ENABLE_BIP39_CZECH 1

#include <key/mnemonic.h>

#include <logging.h>
#include <util/string.h>
#include <util/strencodings.h>
#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>
#include <random.h>

#include <unilib/uninorms.h>
#include <unilib/utf8.h>

#include <map>
#include <atomic>
#include <cmath>

#ifdef ENABLE_BIP39_ENGLISH
#include <key/wordlists/english.h>
#else
unsigned char *english_txt = nullptr;
uint32_t english_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_FRENCH
#include <key/wordlists/french.h>
#else
unsigned char *french_txt = nullptr;
uint32_t french_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_JAPANESE
#include <key/wordlists/japanese.h>
#else
unsigned char *japanese_txt = nullptr;
uint32_t japanese_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_SPANISH
#include <key/wordlists/spanish.h>
#else
unsigned char *spanish_txt = nullptr;
uint32_t spanish_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_CHINESE_S
#include <key/wordlists/chinese_simplified.h>
#else
unsigned char *chinese_simplified_txt = nullptr;
uint32_t chinese_simplified_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_CHINESE_T
#include <key/wordlists/chinese_traditional.h>
#else
unsigned char *chinese_traditional_txt = nullptr;
uint32_t chinese_traditional_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_ITALIAN
#include <key/wordlists/italian.h>
#else
unsigned char *italian_txt = nullptr;
uint32_t italian_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_KOREAN
#include <key/wordlists/korean.h>
#else
unsigned char *korean_txt = nullptr;
uint32_t korean_txt_len = 0;
#endif
#ifdef ENABLE_BIP39_CZECH
#include <key/wordlists/czech.h>
#else
unsigned char *czech_txt = nullptr;
uint32_t czech_txt_len = 0;
#endif

namespace mnemonic {

static const unsigned char *mnLanguages[] =
{
    nullptr,
    english_txt,
    french_txt,
    japanese_txt,
    spanish_txt,
    chinese_simplified_txt,
    chinese_traditional_txt,
    italian_txt,
    korean_txt,
    czech_txt,
};

static const uint32_t mnLanguageLens[] =
{
    0,
    english_txt_len,
    french_txt_len,
    japanese_txt_len,
    spanish_txt_len,
    chinese_simplified_txt_len,
    chinese_traditional_txt_len,
    italian_txt_len,
    korean_txt_len,
    czech_txt_len,
};

const char *mnLanguagesDesc[WLL_MAX] =
{
    nullptr,
    "English",
    "French",
    "Japanese",
    "Spanish",
    "Chinese Simplified",
    "Chinese Traditional",
    "Italian",
    "Korean",
    "Czech",
};

const char *mnLanguagesTag[WLL_MAX] =
{
    nullptr,
    "english",
    "french",
    "japanese",
    "spanish",
    "chinese_s",
    "chinese_t",
    "italian",
    "korean",
    "czech",
};

static void NormaliseUnicode(std::string &str)
{
    if (str.size() < 1) {
        return;
    }
    std::u32string u32;
    ufal::unilib::utf8::decode(str, u32);
    ufal::unilib::uninorms::nfkd(u32);
    ufal::unilib::utf8::encode(u32, str);
}

static void NormaliseInput(std::string &str)
{
    part::TrimWhitespace(str);
    NormaliseUnicode(str);
}

int GetWord(int o, const char *pwl, int max, std::string &sWord)
{
    sWord = "";
    char *pt = (char*)pwl;
    while (o > 0) {
        if (*pt == '\n') {
            o--;
        }
        pt++;
        if (pt >= pwl + max) {
            return 1;
        }
    }

    while (pt < (pwl + max)) {
        if (*pt == '\n') {
            return 0;
        }
        sWord += *pt;
        pt++;
    }

    return 1;
}

int GetWordOffset(const char *p, const char *pwl, int max, int &o)
{
    // List must end with \n
    const char *pt = pwl;
    int l = strlen(p);
    int i = 0;
    int c = 0;
    int f = 1;
    while (pt < (pwl + max)) {
        if (*pt == '\n') {
            if (f && c == l) { // found
                o = i;
                return 0;
            }
            i++;
            c = 0;
            f = 1;
        } else {
            if (c >= l) {
                f = 0;
            } else
            if (f && *(p + c) != *pt) {
                f = 0;
            }
            c++;
        }
        pt++;
    }
    return 1;
}

int GetWordOffsets(int nLanguage, const std::string &sWordList, std::vector<int> &vWordInts, std::string &sError)
{
    if (nLanguage < 1 || nLanguage >= WLL_MAX || !HaveLanguage(nLanguage)) {
        return errorN(1, sError, __func__, "Unknown language");
    }
    const char *pwl = (const char*) mnLanguages[nLanguage];
    int m = mnLanguageLens[nLanguage];
    char tmp[4096]; // msan
    memset(tmp, 0, sizeof(tmp));
    if (sWordList.size() >= 4096) {
        return errorN(1, sError, __func__, "Word string is too long.");
    }
    strcpy(tmp, sWordList.c_str());

    char *p, *token;
    p = strtok_r(tmp, " ", &token);
    while (p != nullptr) {
        int ofs;
        if (0 != GetWordOffset(p, pwl, m, ofs)) {
            sError = strprintf("Unknown word: %s", p);
            return errorN(3, "%s: %s", __func__, sError.c_str());
        }

        vWordInts.push_back(ofs);
        p = strtok_r(nullptr, " ", &token);
    }
    return 0;
}

int CountLanguageWords(const char *pwl, int max) {
    // List must end with \n
    int num_words = 0;
    const char *pt = pwl;
    while (pt < (pwl + max)) {
        if (*pt == '\n') {
            num_words++;
        }
        pt++;
    }
    return num_words;
}

int GetLanguageOffset(std::string sIn)
{
    int nLanguage = -1;
    sIn = ToLower(sIn);

    for (size_t k = 1; k < WLL_MAX; ++k) {
        if (sIn != mnLanguagesTag[k]) {
            continue;
        }
        nLanguage = k;
        break;
    }

    if (nLanguage < 1 || nLanguage >= WLL_MAX || !HaveLanguage(nLanguage)) {
        throw std::runtime_error("Unknown language.");
    }

    return nLanguage;
}

int DetectLanguage(const std::string &sWordList)
{
    // Try detect the language
    // Tolerate spelling mistakes, will be reported in other functions
    char tmp[2048];
    memset(tmp, 0, sizeof(tmp)); // msan
    if (sWordList.size() >= sizeof(tmp)) {
        return errorN(-1, "%s: Word List too long.", __func__);
    }

    for (int l = 1; l < WLL_MAX; ++l) {
        const char *pwl = (const char*) mnLanguages[l];
        if (!pwl) {
            continue;
        }
        int m = mnLanguageLens[l];
        strcpy(tmp, sWordList.c_str());

        // The Chinese dialects have many words in common, match full phrase
        int maxTries = (l == WLL_CHINESE_S || l == WLL_CHINESE_T) ? 24 : 8;

        int nHit = 0;
        int nMiss = 0;
        char *p, *token;
        p = strtok_r(tmp, " ", &token);
        while (p != nullptr) {
            int ofs;
            if (0 == GetWordOffset(p, pwl, m, ofs)) {
                nHit++;
            } else {
                nMiss++;
            }

            if (!maxTries--) {
                break;
            }
            p = strtok_r(nullptr, " ", &token);
        }

        // Chinese dialects overlap too much to tolerate failures
        if ((l == WLL_CHINESE_S || l == WLL_CHINESE_T) &&
            nMiss > 0) {
            continue;
        }

        if (nHit > nMiss && nMiss < 2) { // tolerate max 2 failures
            return l;
        }
    }

    return 0;
}

int Encode(int nLanguage, const std::vector<uint8_t> &vEntropy, std::string &sWordList, std::string &sError)
{
    LogPrint(BCLog::HDWALLET, "%s: language %d.\n", __func__, nLanguage);

    sWordList = "";

    if (nLanguage < 1 || nLanguage >= WLL_MAX || !mnLanguages[nLanguage]) {
        sError = "Unknown language.";
        return errorN(1, "%s: %s", __func__, sError.c_str());
    }

    // Checksum is 1st n bytes of the sha256 hash
    uint8_t hash[32];
    CSHA256().Write(&vEntropy[0], vEntropy.size()).Finalize((uint8_t*)hash);

    int nCsSize = vEntropy.size() / 4; // 32 / 8
    if (nCsSize < 1 || nCsSize > 256) {
        sError = "Entropy bytes out of range.";
        return errorN(2, "%s: %s", __func__, sError.c_str());
    }

    std::vector<uint8_t> vIn = vEntropy;

    int ncb = nCsSize/8;
    int r = nCsSize % 8;
    if (r != 0) {
        ncb++;
    }
    std::vector<uint8_t> vTmp(32);
    memcpy(&vTmp[0], &hash, ncb);
    memset(&vTmp[ncb], 0, 32-ncb);

    vIn.insert(vIn.end(), vTmp.begin(), vTmp.end());

    std::vector<int> vWord;

    int nBits = vEntropy.size() * 8 + nCsSize;

    int i = 0;
    while (i < nBits) {
        int o = 0;
        int s = i / 8;
        int r = i % 8;

        uint8_t b1 = vIn[s];
        uint8_t b2 = vIn[s+1];

        o = (b1 << r) & 0xFF;
        o = o << (11 - 8);

        if (r > 5) {
            uint8_t b3 = vIn[s+2];
            o |= (b2 << (r-5));
            o |= (b3 >> (8-(r-5)));
        } else {
            o |= (int(b2)) >> ((8 - (11 - 8))-r);
        }

        o = o & 0x7FF;

        vWord.push_back(o);
        i += 11;
    }

    const char *pwl = (const char*) mnLanguages[nLanguage];
    int m = mnLanguageLens[nLanguage];

    for (size_t k = 0; k < vWord.size(); ++k) {
        int o = vWord[k];

        std::string sWord;
        if (0 != GetWord(o, pwl, m, sWord)) {
            sError = strprintf("Word extract failed %d, language %d.", o, nLanguage);
            return errorN(3, "%s: %s", __func__, sError.c_str());
        }

        if (sWordList != "") {
            sWordList += " ";
        }
        sWordList += sWord;
    }

    if (nLanguage == WLL_JAPANESE) {
        part::ReplaceStrInPlace(sWordList, " ", "\u3000");
    }

    return 0;
}

int Decode(int &nLanguage, const std::string &sWordListIn, std::vector<uint8_t> &vEntropy, std::string &sError, bool fIgnoreChecksum)
{
    LogPrint(BCLog::HDWALLET, "%s: Language %d.\n", __func__, nLanguage);

    std::string sWordList = sWordListIn;
    NormaliseInput(sWordList);

    if (nLanguage == -1) {
        nLanguage = DetectLanguage(sWordList);
    }

    if (nLanguage < 1 || nLanguage >= WLL_MAX || !mnLanguages[nLanguage]) {
        sError = "Unknown language";
        return errorN(1, "%s: %s", __func__, sError.c_str());
    }

    LogPrint(BCLog::HDWALLET, "%s: Detected language %d.\n", __func__, nLanguage);

    if (sWordList.size() >= 2048) {
        sError = "Word List too long.";
        return errorN(2, "%s: %s", __func__, sError.c_str());
    }

    if (strstr(sWordList.c_str(), "  ") != nullptr) {
        sError = "Multiple spaces between words";
        return errorN(4, "%s: %s", __func__, sError.c_str());
    }

    std::vector<int> vWordInts;
    int rv = GetWordOffsets(nLanguage, sWordList, vWordInts, sError);
    if (0 != rv) {
        return rv;
    }

    if (!fIgnoreChecksum &&
        vWordInts.size() % 3 != 0) {
        sError = "No. of words must be divisible by 3";
        return errorN(4, "%s: %s", __func__, sError.c_str());
    }

    int nBits = vWordInts.size() * 11;
    int nBytes = nBits/8 + (nBits % 8 == 0 ? 0 : 1);
    vEntropy.resize(nBytes);

    memset(&vEntropy[0], 0, nBytes);

    int i = 0;
    size_t wl = vWordInts.size();
    size_t el = vEntropy.size();
    for (size_t k = 0; k < wl; ++k) {
        int o = vWordInts[k];

        int s = i / 8;
        int r = i % 8;

        vEntropy[s] |= (o >> (r+3)) & 0x7FF;

        if (s < int(el)-1) {
            if (r > 5) {
                vEntropy[s+1] |= (uint8_t) ((o >> (r-5))) & 0x7FF;
                if (s < int(el)-2) {
                    vEntropy[s+2] |= (uint8_t) (o << (8-(r-5))) & 0x7FF;
                }
            } else {
                vEntropy[s+1] |= (uint8_t) (o << (5-r)) & 0x7FF;
            }
        }
        i += 11;
    }

    if (fIgnoreChecksum) {
        return 0;
    }

    // Checksum
    int nLenChecksum = nBits / 32;
    int nLenEntropy = nBits - nLenChecksum;

    int nBytesEntropy = nLenEntropy / 8;
    int nBytesChecksum = nLenChecksum / 8 + (nLenChecksum % 8 == 0 ? 0 : 1);

    std::vector<uint8_t> vCS;

    vCS.resize(nBytesChecksum);
    memcpy(&vCS[0], &vEntropy[nBytesEntropy], nBytesChecksum);

    vEntropy.resize(nBytesEntropy);

    uint8_t hash[32];
    CSHA256().Write(&vEntropy[0], vEntropy.size()).Finalize((uint8_t*)hash);

    std::vector<uint8_t> vCSTest;

    vCSTest.resize(nBytesChecksum);
    memcpy(&vCSTest[0], &hash, nBytesChecksum);

    int r = nLenChecksum % 8;

    if (r > 0) {
        vCSTest[nBytesChecksum-1] &= (((1<<r)-1) << (8-r));
    }
    if (vCSTest != vCS) {
        sError = "Checksum mismatch.";
        return errorN(5, "%s: %s", __func__, sError.c_str());
    }

    return 0;
};

static int mnemonicKdf(const uint8_t *password, size_t lenPassword,
    const uint8_t *salt, size_t lenSalt, size_t nIterations, uint8_t *out)
{
    /*
    https://tools.ietf.org/html/rfc2898
    5.2 PBKDF2

    F (P, S, c, i) = U_1 \xor U_2 \xor ... \xor U_c
    where
        U_1 = PRF (P, S || INT (i)) ,
        U_2 = PRF (P, U_1) ,
        ...
        U_c = PRF (P, U_{c-1}) .
    */

    // Output length is always 64bytes, only 1 block

    if (nIterations < 1) {
        return 1;
    }

    uint8_t r[64];

    const unsigned char one_be[4] = {0, 0, 0, 1};
    CHMAC_SHA512 ctx(password, lenPassword);
    CHMAC_SHA512 ctx_state = ctx;
    ctx.Write(salt, lenSalt);
    ctx.Write(one_be, 4);
    ctx.Finalize(r);
    memcpy(out, r, 64);

    for (size_t k = 1; k < nIterations; ++k) {
        ctx= ctx_state;
        ctx.Write(r, 64);
        ctx.Finalize(r);

        for (size_t i = 0; i < 64; ++i) {
            out[i] ^= r[i];
        }
    }

    return 0;
};

int ToSeed(const std::string &sMnemonic, const std::string &sPasswordIn, std::vector<uint8_t> &vSeed)
{
    LogPrint(BCLog::HDWALLET, "%s\n", __func__);

    vSeed.resize(64);

    std::string sWordList = sMnemonic, sPassword = sPasswordIn;
    NormaliseInput(sWordList);
    NormaliseInput(sPassword);

    if (strstr(sWordList.c_str(), "  ") != nullptr) {
        return errorN(1, "%s: Multiple spaces between words.", __func__);
    }

    int nIterations = 2048;

    std::string sSalt = std::string("mnemonic") + sPassword;

    if (0 != mnemonicKdf((uint8_t*)sWordList.data(), sWordList.size(),
        (uint8_t*)sSalt.data(), sSalt.size(), nIterations, &vSeed[0])) {
        return errorN(1, "%s: mnemonicKdf failed.", __func__);
    }

    return 0;
};

int AddChecksum(int nLanguage, const std::string &sWordListIn, std::string &sWordListOut, std::string &sError)
{
    std::string sWordList = sWordListIn;
    NormaliseInput(sWordList);

    sWordListOut = "";
    if (nLanguage == -1) {
        nLanguage = DetectLanguage(sWordList); // Needed here for MnemonicEncode, MnemonicDecode will complain if in error
    }

    int rv;
    std::vector<uint8_t> vEntropy;
    if (0 != (rv = Decode(nLanguage, sWordList, vEntropy, sError, true))) {
        return rv;
    }
    if (0 != (rv = Encode(nLanguage, vEntropy, sWordListOut, sError))) {
        return rv;
    }
    if (0 != (rv = Decode(nLanguage, sWordListOut, vEntropy, sError))) {
        return rv;
    }

    return 0;
};

int GetWord(int nLanguage, int nWord, std::string &sWord, std::string &sError)
{
    if (nLanguage < 1 || nLanguage >= WLL_MAX || !mnLanguages[nLanguage]) {
        sError = "Unknown language.";
        return errorN(1, "%s: %s", __func__, sError.c_str());
    }

    const char *pwl = (const char*) mnLanguages[nLanguage];
    int m = mnLanguageLens[nLanguage];

    if (0 != GetWord(nWord, pwl, m, sWord)) {
        sError = strprintf("Word extract failed %d, language %d.", nWord, nLanguage);
        return errorN(3, "%s: %s", __func__, sError.c_str());
    }

    return 0;
};

std::string GetLanguage(int nLanguage)
{
    if (nLanguage < 1 || nLanguage >= WLL_MAX || !mnLanguages[nLanguage]) {
        return "Unknown";
    }

    return mnLanguagesDesc[nLanguage];
};

std::string ListEnabledLanguages(std::string separator)
{
    std::string enabled_languages;
    for (size_t k = 1; k < WLL_MAX; ++k) {
        if (!HaveLanguage(k)) {
            continue;
        }
        if (enabled_languages.size()) {
            enabled_languages += separator;
        }
        enabled_languages += mnLanguagesTag[k];
    }
    return enabled_languages;
};

bool HaveLanguage(int nLanguage){
    return mnLanguages[nLanguage];
}

} // namespace mnemonic


namespace shamir39 {

const int num_bits = 11;
const int max_bits_value = (1 << num_bits) - 1;
int exp_table[2048]; // 2 ^ num_bits
int log_table[2048];

std::atomic<bool> built_tables{false};
std::atomic<bool> building_tables{false};

/** Return the smallest number n such that (x >> n) == 0 (or 64 if the highest bit in x is set. */
int static inline CountBits(int x)
{
    // TODO: Use __builtin_clz
    int ret = 0;
    while (x) {
        x >>= 1;
        ++ret;
    }
    return ret;
}

void StrongRandomIssuer::RefillCache()
{
    GetStrongRandBytes2(m_cached_bytes, m_max_bytes);
    m_bytes_used = 0;
    m_bits_used = 0;
}

int StrongRandomIssuer::GetBits(size_t num_bits, int &output)
{
    output = 0;
    if (num_bits > 32) {
        return 1;
    }
    for (size_t k = 0; k < num_bits; ++k) {
        if (m_bytes_used >= m_max_bytes) {
            RefillCache();
        }
        if (m_cached_bytes[m_bytes_used] & (1 << m_bits_used)) {
            output |= 1 << k;
        }
        m_bits_used++;
        if (m_bits_used >= 8) {
            m_bytes_used++;
            m_bits_used = 0;
        }
    }
    return 0;
}

static int build_tables()
{
    if (built_tables) {
        return 0;
    }
    if (building_tables) {
        LogPrintf("Error: Shamir39 tables are being initialised.\n");
        return 1;
    }
    building_tables = true;

    // https://github.com/iancoleman/shamir39/blob/cfc89c4fd24d360ee57e2158e6572d7042de580c/src/js/shamir39.js#L291
    int size = 1 << num_bits;
    int x = 1;
    int primitive = 5;
    for (int i = 0; i < size; ++i) {
        exp_table[i] = x;
        log_table[x] = i;
        x <<= 1;
        if (x >= size) {
            x ^= primitive;
            x &= size - 1;
        }
    }

    built_tables = true;
    return 0;
}

static int horner(int x, std::vector<int> &coeffs)
{
    // https://github.com/iancoleman/shamir39/blob/cfc89c4fd24d360ee57e2158e6572d7042de580c/src/js/shamir39.js#L521
    // Polynomial evaluation at `x` using Horner's Method
    // TODO: this can possibly be sped up using other methods
    // NOTE: fx=fx * x + coeff[i] ->  exp(log(fx) + log(x)) + coeff[i],
    //       so if fx===0, just set fx to coeff[i] because
    //       using the exp/log form will result in incorrect value
    int logx = log_table[x];
    int fx = 0;
    int max = (1 << num_bits) - 1;
    for (int i = coeffs.size() - 1; i >= 0; i--) {
        if (fx == 0) {
            fx = coeffs[i];
            continue;
        }
        fx = exp_table[ (logx + log_table[fx]) % max ] ^ coeffs[i];
    }
    return fx;
}

int splitmnemonic(const std::string mnemonic_in, int language_ind, size_t num_shares, size_t threshold, std::vector<std::string> &output, std::string &sError)
{
    output.clear();
    if (num_shares < 2 || num_shares > max_bits_value) {
        return errorN(1, sError, __func__, "Number of shares must be at least 2 and at most 4095");
    }
    if (threshold < 2 || threshold > num_shares) {
        return errorN(1, sError, __func__, "Required shares must be at least 2 and at most num_shares");
    }

    std::string word_list = mnemonic_in;
    mnemonic::NormaliseInput(word_list);

    // Detect language if not specified
    if (language_ind < 0) {
        language_ind = mnemonic::DetectLanguage(word_list);
        if (language_ind < 0) {
            return errorN(2, sError, __func__, "Language detection failed");
        }
    }
    if (language_ind < 1 || language_ind >= mnemonic::WLL_MAX || !mnemonic::HaveLanguage(language_ind)) {
        return errorN(2, sError, __func__, "Unknown language");
    }

    LogPrint(BCLog::HDWALLET, "%s: Using language %d.\n", __func__, language_ind);
    const char *pwl = (const char*) mnemonic::mnLanguages[language_ind];
    int language_data_length = mnemonic::mnLanguageLens[language_ind];
    int num_words = mnemonic::CountLanguageWords(pwl, language_data_length);

    if (num_words != 2048) {
        return errorN(2, sError, __func__, "Wordlist must have exactly 2048 words");
    }

    std::vector<int> word_offsets;
    if (0 != mnemonic::GetWordOffsets(language_ind, word_list, word_offsets, sError)) {
        return 1;
    }
    // convert bip39 mnemonic into bits

    if (0 != build_tables()) {
        return errorN(4, sError, __func__, "build_tables failed");
    }

    // Add padding word for compatibility with
    // https://iancoleman.io/shamir39/
    int bits_length = word_offsets.size() * num_bits;
    int zero_pad = (std::ceil((float)bits_length / 4.0) * 4) - bits_length;
    int pad_word = 1 << zero_pad;
    word_offsets.insert(word_offsets.begin(), pad_word);

    StrongRandomIssuer random_issuer;
    std::vector<std::vector<int> > shares(num_shares);
    for (int i = word_offsets.size() - 1; i >= 0; i--) {
        std::vector<int> coeffs(threshold + 1);
        coeffs[0] = word_offsets[i];
        for (int k = 1; k < int(threshold); ++k) {
            if (0 != random_issuer.GetBits(11, coeffs[k])) {
                return errorN(5, sError, __func__, "Get random bits failed");
            }
        }

        for (int k = 1; k < int(num_shares) + 1; k++) {
            int y = horner(k, coeffs);
            shares[k - 1].push_back(y);
        }
    }

    for (int i = 0; i < int(num_shares); i++) {
        std::string sWord, sWordList = "shamir39-p1";

        // Pack parameters in prefix words, 5 bytes each in each word + run-on indicator bit
        int params_words = std::ceil((float)std::max(CountBits(threshold), CountBits(i)) / 5.0);

        for (int k = 0; k < params_words; k++) {
            int shifted_m = (threshold >> (5 * k)) & 0x1F;
            int shifted_i = (i >> (5 * k)) & 0x1F;
            int params_word = (shifted_m << 5) + (shifted_i & 0x1F);
            if (k > 0) {
                params_word |= 1 << 10;
            }
            shares[i].push_back(params_word);
        }

        for (int k = shares[i].size() - 1; k >= 0 ; k--) {
            int o = shares[i][k];
            if (0 != mnemonic::GetWord(o, pwl, language_data_length, sWord)) {
                return errorN(3, sError, __func__, strprintf("Word extract failed %d, language %d.", o, language_ind).c_str());
            }
            if (sWordList != "") {
                sWordList += " ";
            }
            sWordList += sWord;
        }
        if (language_ind == mnemonic::WLL_JAPANESE) {
            part::ReplaceStrInPlace(sWordList, " ", "\u3000");
        }
        output.push_back(sWordList);
    }

    return 0;
}

int lagrange(int word_index, const std::vector<int> &share_indices, const std::map<int, std::vector<int> > &shamir_shares)
{
    int at = 0; // always 0?
    int sum = 0;
    for (size_t i = 0; i < share_indices.size(); i++) {
        const auto it = shamir_shares.find(share_indices[i]);
        assert(it != shamir_shares.end());

        int share_word_index = it->second[word_index];
        if (share_word_index == 0) {
            continue;
        }
        int product = log_table[share_word_index];

        for (size_t j = 0; j < share_indices.size(); j++) {
            if (i == j) {
                continue;
            }
            int xi = share_indices[i] + 1;
            int xj = share_indices[j] + 1;
            product = (product + log_table[at ^ xj] - log_table[xi ^ xj] + max_bits_value /* to make sure it's not negative */ ) % max_bits_value;
        }
        sum = sum ^ exp_table[product];
    }

    return sum;
}

int combinemnemonic(const std::vector<std::string> &mnemonics_in, int language_ind, std::string &mnemonic_out, std::string &sError)
{
    mnemonic_out.clear();
    if (mnemonics_in.size() < 2) {
        return errorN(1, sError, __func__, "Too few mnemonics provided");
    }

    if (0 != build_tables()) {
        return errorN(4, sError, __func__, "build_tables failed");
    }

    std::map<int, std::vector<int> > shamir_shares;
    int group_threshold = 0;
    for (size_t i = 0; i < mnemonics_in.size(); i++) {
        std::string words = mnemonics_in[i];

        const char *version_word = "shamir39-p1";
        if (words.size() < strlen(version_word) ||
            strncmp(words.data(), version_word, strlen(version_word)) != 0) {
            return errorN(1, sError, __func__, "Invalid version word");
        }
        words = words.substr(strlen(version_word));
        mnemonic::NormaliseInput(words);

        if (language_ind < 0) {
            language_ind = mnemonic::DetectLanguage(words);
            if (language_ind < 0) {
                return errorN(2, sError, __func__, "Language detection failed");
            }
        }
        if (language_ind < 1 || language_ind >= mnemonic::WLL_MAX || !mnemonic::HaveLanguage(language_ind)) {
            return errorN(2, sError, __func__, "Unknown language");
        }

        std::vector<int> word_offsets;
        if (0 != mnemonic::GetWordOffsets(language_ind, words, word_offsets, sError)) {
            return 1;
        }

        // Extract parameters
        int threshold = 0;
        int mnemonic_index = 0;
        int last_param_word = 0;
        for (size_t k = 0; k < word_offsets.size(); ++k) {
            if (!(word_offsets[k] & (1 << 10))) {
                last_param_word = k;
                break;
            }
        }
        for (int k = 0; k <= last_param_word; ++k) {
            int word_index = word_offsets[k] & 0x3FF; // Strip run-on bit
            int shift_bits = (5 * (last_param_word-k));
            threshold += ((word_index >> 5) & 0x1F) << shift_bits;
            mnemonic_index += (word_index & 0x1F) << shift_bits;
        }
        if (threshold < 2 || threshold > max_bits_value) {
            return errorN(2, sError, __func__, "Threshold out of valid range");
        }
        if (mnemonic_index < 0 || mnemonic_index > max_bits_value) {
            return errorN(2, sError, __func__, "Mnemonic index out of valid range");
        }
        if (group_threshold == 0) {
            group_threshold = threshold;
        }
        if (group_threshold != threshold) {
            return errorN(2, sError, __func__, "Mixed thresholds in mnemonic group");
        }

        // Strip parameter word/s and padding word
        word_offsets.erase(word_offsets.begin(), word_offsets.begin() + last_param_word + 2);
        shamir_shares[mnemonic_index] = word_offsets;
    }

    if (shamir_shares.size() < 2 || int(shamir_shares.size()) < group_threshold) {
        return errorN(2, sError, __func__, "Too few shares for threshold");
    }

    int num_share_words = 0;

    std::vector<int> share_indices;
    for (auto const &share : shamir_shares) {
        int share_index = share.first;
        const std::vector<int> &word_offsets = share.second;
        if (num_share_words == 0) {
            num_share_words = word_offsets.size();
        }
        if (num_share_words != int(word_offsets.size())) {
            return errorN(2, sError, __func__, "Mismatched share length");
        }
        share_indices.push_back(share_index);
    }

    const char *pwl = (const char*) mnemonic::mnLanguages[language_ind];
    int language_data_length = mnemonic::mnLanguageLens[language_ind];

    std::string sWord;
    for (int i = 0; i < num_share_words; i++) {
        int word_offset = lagrange(i, share_indices, shamir_shares);
        if (0 != mnemonic::GetWord(word_offset, pwl, language_data_length, sWord)) {
            return errorN(3, sError, __func__, strprintf("Word extract failed %d, language %d.", word_offset, language_ind).c_str());
        }
        if (mnemonic_out != "") {
            mnemonic_out += " ";
        }
        mnemonic_out += sWord;
    }

    if (language_ind == mnemonic::WLL_JAPANESE) {
        part::ReplaceStrInPlace(mnemonic_out, " ", "\u3000");
    }

    return 0;
}

} // namespace shamir39
