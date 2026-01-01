#include "util.hpp"
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstring>
#include <stdexcept>

namespace util {

int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::string epoch_to_iso8601(int64_t epoch_seconds) {
    std::time_t time = static_cast<std::time_t>(epoch_seconds);
    std::tm tm_time;
    localtime_r(&time, &tm_time);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_time, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

int64_t iso8601_to_epoch(const std::string& iso_str) {
    std::tm tm_time = {};
    std::istringstream iss(iso_str);
    iss >> std::get_time(&tm_time, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) {
        return 0;
    }
    return static_cast<int64_t>(mktime(&tm_time));
}

std::string today_yyyy_mm_dd() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d");
    return oss.str();
}

std::string base64_encode(const unsigned char* data, size_t len) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    BIO_write(bio, data, static_cast<int>(len));
    BIO_flush(bio);
    
    BUF_MEM* bptr;
    BIO_get_mem_ptr(bio, &bptr);
    
    std::string result(bptr->data, bptr->length);
    BIO_free_all(bio);
    
    return result;
}

std::string base64_encode(const std::string& data) {
    return base64_encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

std::string base64_decode(const std::string& encoded) {
    BIO* bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    
    std::string result(encoded.size(), '\0');
    int decoded_len = BIO_read(bio, &result[0], static_cast<int>(encoded.size()));
    BIO_free_all(bio);
    
    if (decoded_len < 0) {
        return "";
    }
    result.resize(static_cast<size_t>(decoded_len));
    return result;
}

std::string sha256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);
    return base64_encode(hash, SHA256_DIGEST_LENGTH);
}

std::string sha256_raw(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);
    return std::string(reinterpret_cast<char*>(hash), SHA256_DIGEST_LENGTH);
}

std::string hmac_sha512(const std::string& key, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    HMAC(EVP_sha512(),
         key.c_str(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.size(),
         hash, &hash_len);
    
    return base64_encode(hash, hash_len);
}

std::string hmac_sha512_raw(const std::string& key_raw, const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    HMAC(EVP_sha512(),
         key_raw.data(), static_cast<int>(key_raw.size()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.size(),
         hash, &hash_len);
    
    return base64_encode(hash, hash_len);
}

std::string generate_nonce() {
    return std::to_string(now_epoch_ms());
}

std::string url_encode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : str) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

int64_t random_jitter_ms(int64_t max_jitter_ms) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int64_t> dist(0, max_jitter_ms);
    return dist(gen);
}

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

bool approx_zero(double val, double epsilon) {
    return std::abs(val) < epsilon;
}

bool approx_equal(double a, double b, double epsilon) {
    return std::abs(a - b) < epsilon;
}

} // namespace util

