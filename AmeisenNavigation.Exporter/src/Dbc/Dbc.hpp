#pragma once

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

class Dbc
{
    unsigned char* Data;
    unsigned int Size;

public:
    inline DbcHeader* GetHeader() const noexcept { return reinterpret_cast<DbcHeader*>(Data); }
    constexpr inline unsigned char* GetData() const noexcept { return Data + sizeof(DbcHeader); }

    inline unsigned int GetRecordCount() const noexcept { return GetHeader()->recordCount; }
    inline unsigned int GetFieldCount() const noexcept { return GetHeader()->fieldCount; }
    inline unsigned int GetRecordSize() const noexcept { return GetHeader()->recordSize; }
    inline unsigned int GetStringTableSize() const noexcept { return GetHeader()->stringTableSize; }
    inline unsigned char* GetStringTable() const noexcept { return GetData() + (GetRecordSize() * GetRecordCount()); }

    template<typename T>
    constexpr inline T Read(unsigned int id, unsigned int field) noexcept
    {
        return *((T*)(GetData() + (id * GetRecordSize() + (field * 4))));
    }

    inline const char* ReadString(unsigned int id, unsigned int field) noexcept
    {
        return reinterpret_cast<char*>(GetStringTable() + Read<unsigned int>(id, field));
    }
};
