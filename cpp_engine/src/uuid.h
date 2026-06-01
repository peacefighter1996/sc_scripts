#pragma once

#include <cstdint>
#include <string>
#include <array>



// rfc9562 uuid layout (128 bits total):
struct uuid {
    uint64_t high;
    uint64_t low;

    std::string to_string() const;
    static uuid from_string(const std::string& s);
    std::array<uint8_t, 16> to_bytes() const;
    static uuid from_bytes(const uint8_t* bytes, size_t len);
    static uuid from_bytes(const std::array<uint8_t, 16>& bytes);
    static uuid generate_uuid_v4();
    char* c_str() const;
};

static const uuid nil_uuid{0, 0};
static const uuid max_uuid{0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF};



// uuid generate_uuid_v7();
// std::string uuid_to_string(const uuid& id);
// std::array<uint8_t, 16> uuid_to_bytes(const uuid& id);
// uuid uuid_from_string(const std::string& s);
// uuid uuid_from_bytes(const std::array<uint8_t, 16>& bytes);
// uuid uuid_from_bytes(const uint8_t* bytes, size_t len);

// overload function for comparison operators to allow using uuid as map keys, etc.
inline bool operator==(const uuid& a, const uuid& b) {
    return a.high == b.high && a.low == b.low;
}

inline bool operator!=(const uuid& a, const uuid& b) {
    return !(a == b);
}
