#pragma once

struct DbcHeader
{
    char magic[4];
    unsigned int recordCount;
    unsigned int fieldCount;
    unsigned int recordSize;
    unsigned int stringTableSize;
};

class DbcFile
{
    unsigned char* Data;

public:
    DbcFile(unsigned char* data) noexcept
        : Data(data)
    {}

    ~DbcFile() noexcept
    {
        if (Data) free(Data);
    }

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
