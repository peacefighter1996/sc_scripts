#include "uuid.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <array>
#include <stdexcept>

#if defined(SCOUT_USE_LIBSODIUM)
#include <sodium.h>
#endif

// https://www.rfc-editor.org/rfc/rfc9562.html

// UUIDv4 is meant for generating UUIDs from truly random or pseudorandom numbers.

// An implementation may generate 128 bits of random data that is used to fill out the UUID fields in Figure 8. The UUID version and variant then replace the respective bits as defined by Sections 4.1 and 4.2.

// Alternatively, an implementation MAY choose to randomly generate the exact required number of bits for random_a, random_b, and random_c (122 bits total) and then concatenate the version and variant in the required position.

// For guidelines on random data generation, see Section 6.9.

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           random_a                            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          random_a             |  ver  |       random_b        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |var|                       random_c                            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           random_c                            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// Figure 8: UUIDv4 Field and Bit Layout
// random_a:
// The first 48 bits of the layout that can be filled with random data as specified in Section 6.9. Occupies bits 0 through 47 (octets 0-5).
// ver:
// The 4-bit version field as defined by Section 4.2, set to 0b0100 (4). Occupies bits 48 through 51 of octet 6.
// random_b:
// 12 more bits of the layout that can be filled random data as per Section 6.9. Occupies bits 52 through 63 (octets 6-7).
// var:
// The 2-bit variant field as defined by Section 4.1, set to 0b10. Occupies bits 64 and 65 of octet 8.
// random_c:
// The final 62 bits of the layout immediately following the var field to be filled with random data as per Section 6.9. Occupies bits 66 through 127 (octets 8-15).

uuid uuid::generate_uuid_v4() {
    uuid id{};
#if defined(SCOUT_USE_LIBSODIUM)
    if (sodium_init() >= 0) {
        unsigned char buf[16];
        randombytes_buf(buf, sizeof(buf));
        // set UUIDv4 version (0100) and RFC 4122 variant (10xxxxxx)
        buf[6] = (buf[6] & 0x0F) | 0x40; // version 4
        buf[8] = (buf[8] & 0x3F) | 0x80; // variant 10

        for (int i = 0; i < 8; ++i) {
            id.high = (id.high << 8) | static_cast<uint64_t>(buf[i]);
        }
        for (int i = 8; i < 16; ++i) {
            id.low = (id.low << 8) | static_cast<uint64_t>(buf[i]);
        }
        return id;
    }
#endif
    // Fallback to std random if libsodium unavailable or init fails
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    uint64_t random_a = dis(gen) & 0x0000FFFFFFFFFFFF; // 48 bits
    uint64_t random_b = dis(gen) & 0x0000000000000FFF;
    uint64_t random_c = dis(gen) & 0x3FFFFFFFFFFFFFFF; // 62 bits
    uint64_t version = 0x4; // version 4
    uint64_t variant = 0x2; // variant 10

    id.high = (random_a << 16) | (version << 12) | random_b;
    id.low = (variant << 62) | random_c;
    return id;
}

std::array<uint8_t, 16> uuid::to_bytes() const {
    std::array<uint8_t, 16> bytes;
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>((this->high >> ((7 - i) * 8)) & 0xFF);
    }
    for (int i = 0; i < 8; ++i) {
        bytes[8 + i] = static_cast<uint8_t>((this->low >> ((7 - i) * 8)) & 0xFF);
    }
    return bytes;
}

uuid uuid::from_bytes(const std::array<uint8_t, 16>& bytes) {
    uuid id{};
    for (int i = 0; i < 8; ++i) {
        id.high = (id.high << 8) | static_cast<uint64_t>(bytes[i]);
    }
    for (int i = 8; i < 16; ++i) {
        id.low = (id.low << 8) | static_cast<uint64_t>(bytes[i]);
    }
    return id;
}

uuid uuid::from_bytes(const uint8_t* bytes, size_t len) {
    if (len != 16) throw std::invalid_argument("UUID byte array must be exactly 16 bytes");
    uuid id{};
    for (size_t i = 0; i < 8; ++i) {
        id.high = (id.high << 8) | static_cast<uint64_t>(bytes[i]);
    }
    for (size_t i = 8; i < 16; ++i) {
        id.low = (id.low << 8) | static_cast<uint64_t>(bytes[i]);
    }
    return id;
}

std::string uuid::to_string() const {
    std::stringstream ss;
    // format example: f81d4fae-7dec-11d0-a765-00a0c91e6bf6

    ss << std::hex << std::setfill('0')
       << std::setw(8) << (this->high >> 32)
       << '-'
       << std::setw(4) << ((this->high >> 16) & 0xFFFF)
       << '-'
       << std::setw(4) << (this->high & 0xFFFF)
       << '-'
       << std::setw(4) << (this->low >> 48)
       << '-'
       << std::setw(12) << (this->low & 0xFFFFFFFFFFFF);

    return ss.str();
}

char* uuid::c_str() const {
    static thread_local std::string str;
    str = this->to_string();
    return const_cast<char*>(str.c_str());
}

uuid uuid::from_string(const std::string& s) {
    // Accept forms with or without braces, with or without hyphens
    std::string hex;
    for (char c : s) {
        if (c == '{' || c == '}') continue;
        if (c == '-') continue;
        if (std::isxdigit(static_cast<unsigned char>(c))) hex.push_back(c);
        else if (std::isspace(static_cast<unsigned char>(c))) continue;
        else throw std::invalid_argument("Invalid character ["+ std::string(1, c)  +"] in UUID string: " + s);
    }

    if (hex.size() != 32) throw std::invalid_argument("UUID string must contain 32 hex digits: " + s);

    std::array<uint8_t, 16> bytes;
    auto hex_val = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        throw std::invalid_argument("Invalid hex digit in UUID string");
    };

    for (size_t i = 0; i < 16; ++i) {
        uint8_t hi = hex_val(hex[2 * i]);
        uint8_t lo = hex_val(hex[2 * i + 1]);
        bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    return uuid::from_bytes(bytes);
}