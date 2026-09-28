#pragma once

#include <cstdint>
#include <cstring>

#pragma pack(push, 1)
struct DbcHeader
{
    char magic[4];
    unsigned int recordCount;
    unsigned int fieldCount;
    unsigned int recordSize;
    unsigned int stringTableSize;
};
#pragma pack(pop)

/// Read-only view of a WDBC file. Layout compatible with CachedFileEntry (Data, Size).
/// All reads are bounds checked, out of range reads return 0 / "".
class Dbc
{
    unsigned char* Data;
    unsigned int Size;

public:
    /// True if the header is valid ("WDBC") and the file is large enough for all records and the string table.
    bool IsValid() const noexcept
    {
        if (!Data || Size < sizeof(DbcHeader) || std::memcmp(Data, "WDBC", 4) != 0)
        {
            return false;
        }

        const auto* h = GetHeader();
        const uint64_t required = sizeof(DbcHeader) + static_cast<uint64_t>(h->recordCount) * h->recordSize
                                  + h->stringTableSize;
        return required <= Size && h->recordSize >= h->fieldCount * 4u;
    }

    const DbcHeader* GetHeader() const noexcept { return reinterpret_cast<const DbcHeader*>(Data); }
    const unsigned char* GetData() const noexcept { return Data + sizeof(DbcHeader); }

    unsigned int GetRecordCount() const noexcept { return GetHeader()->recordCount; }
    unsigned int GetFieldCount() const noexcept { return GetHeader()->fieldCount; }
    unsigned int GetRecordSize() const noexcept { return GetHeader()->recordSize; }
    unsigned int GetStringTableSize() const noexcept { return GetHeader()->stringTableSize; }

    const unsigned char* GetStringTable() const noexcept
    {
        return GetData() + static_cast<size_t>(GetRecordSize()) * GetRecordCount();
    }

    template <typename T>
    T Read(unsigned int record, unsigned int field) const noexcept
    {
        static_assert(sizeof(T) <= 4, "DBC fields are 4 bytes");

        if (record >= GetRecordCount() || static_cast<uint64_t>(field) * 4 + sizeof(T) > GetRecordSize())
        {
            return T{};
        }

        T value{};
        std::memcpy(&value, GetData() + static_cast<size_t>(record) * GetRecordSize() + field * 4u, sizeof(T));
        return value;
    }

    const char* ReadString(unsigned int record, unsigned int field) const noexcept
    {
        const auto offset = Read<unsigned int>(record, field);

        if (offset >= GetStringTableSize())
        {
            return "";
        }

        const char* str = reinterpret_cast<const char*>(GetStringTable() + offset);

        // Make sure the string is terminated inside the table.
        return std::memchr(str, 0, GetStringTableSize() - offset) ? str : "";
    }
};
