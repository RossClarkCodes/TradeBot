#ifndef UTIL_HPP
#define UTIL_HPP

#include <string>
#include <cstdint>
#include <chrono>
#include <random>

namespace util {

// Time utilities
int64_t now_epoch_seconds();
int64_t now_epoch_ms();
std::string now_iso8601();
std::string epoch_to_iso8601(int64_t epoch_seconds);
int64_t iso8601_to_epoch(const std::string& iso_str);
std::string today_yyyy_mm_dd();

// Cryptographic utilities for Kraken API
std::string base64_encode(const unsigned char* data, size_t len);
std::string base64_encode(const std::string& data);
std::string base64_decode(const std::string& encoded);
std::string sha256(const std::string& data);
std::string sha256_raw(const std::string& data);
std::string hmac_sha512(const std::string& key, const std::string& data);
std::string hmac_sha512_raw(const std::string& key_raw, const std::string& data);

// Nonce generation
std::string generate_nonce();

// URL encoding
std::string url_encode(const std::string& str);

// Random jitter for backoff
int64_t random_jitter_ms(int64_t max_jitter_ms);

// String utilities
std::string trim(const std::string& str);
bool file_exists(const std::string& path);

// Safe double comparison
bool approx_zero(double val, double epsilon = 1e-9);
bool approx_equal(double a, double b, double epsilon = 1e-9);

} // namespace util

#endif // UTIL_HPP

