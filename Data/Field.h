#pragma once

#include <cstddef>
#include <cstdint>

enum class FieldType
{
    UInt8,
    UInt16,
    UInt32,
    UInt64,

    Int8,
    Int16,
    Int32,
    Int64,

    Float,
    Double,

    CharArray
};

enum class VersionFlags : uint8_t
{
    None = 0,
    Old  = 1 << 0,
    New  = 1 << 1,
    Both = Old | New // Pre-GS7 and GS7+
};

inline constexpr VersionFlags operator|(VersionFlags lhs, VersionFlags rhs)
{
    return static_cast<VersionFlags>(
        static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

inline constexpr VersionFlags operator&(VersionFlags lhs, VersionFlags rhs)
{
    return static_cast<VersionFlags>(
        static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

inline constexpr bool HasVersion(VersionFlags flags, VersionFlags version)
{
    return (flags & version) != VersionFlags::None;
}

struct Field
{
    const char*  name;
    FieldType    type;
    std::size_t  objectOffset;
    std::size_t  fileSize;
    VersionFlags versions;
};