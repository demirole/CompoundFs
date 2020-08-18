
#pragma once

#include "CacheManager.h"
#include "FreeStore.h"
#include "BTree.h"
#include "DirectoryObjects.h"
#include <memory>
#include <cstdint>

namespace TxFs
{

enum class Folder : uint32_t;

///////////////////////////////////////////////////////////////////////////////

class DirectoryKey
{
public:
    static constexpr Folder Root { 0 };

public:
    DirectoryKey(std::string_view name)
    {
        m_key.pushBack(Root);
        m_key.pushBack(name);
    }

    DirectoryKey(Folder folder, std::string_view name)
    {
        m_key.pushBack(folder);
        m_key.pushBack(name);
    }

    DirectoryKey(Folder folder) noexcept { m_key.pushBack(folder); }

    constexpr ByteStringView asByteStringView() const noexcept { return m_key; }

private:
    MutableByteString m_key;
};

///////////////////////////////////////////////////////////////////////////////

class DirectoryStructure
{
public:
    class Cursor;

public:
    DirectoryStructure(const std::shared_ptr<CacheManager>& cacheManager, FileDescriptor freeStore,
                       PageIndex rootIndex = PageIdx::INVALID, uint32_t maxFolderId = 1);

    std::optional<Folder> makeSubFolder(const DirectoryKey& dkey);
    std::optional<Folder> subFolder(const DirectoryKey& dkey) const;

    bool addAttribute(const DirectoryKey& dkey, const ByteStringOps::Variant& attribute);
    std::optional<ByteStringOps::Variant> getAttribute(const DirectoryKey& dkey) const;

    size_t remove(const DirectoryKey& dkey);
    size_t remove(ByteStringView key);
    size_t remove(Folder folder);

    std::optional<FileDescriptor> openFile(const DirectoryKey& dkey) const;
    bool createFile(const DirectoryKey& dkey);
    std::optional<FileDescriptor> appendFile(const DirectoryKey& dkey);
    bool updateFile(const DirectoryKey& dkey, FileDescriptor desc);

    Cursor find(const DirectoryKey& dkey) const;
    Cursor begin(const DirectoryKey& dkey) const;
    Cursor next(Cursor cursor) const;

    void commit();

private:
    std::shared_ptr<CacheManager> m_cacheManager;
    BTree m_btree;
    uint32_t m_maxFolderId;
    FreeStore m_freeStore;
};

//////////////////////////////////////////////////////////////////////////

class DirectoryStructure::Cursor
{
    friend class DirectoryStructure;

public:
    constexpr Cursor() noexcept = default;
    Cursor(const BTree::Cursor& cursor) noexcept
        : m_cursor(cursor)
    {}

    constexpr bool operator==(const Cursor& rhs) const noexcept { return m_cursor == rhs.m_cursor; }
    constexpr bool operator!=(const Cursor& rhs) const noexcept { return !(m_cursor == rhs.m_cursor); }

    std::pair<Folder,std::string_view> key() const;
    ByteStringOps::Variant value() const { return ByteStringOps::toVariant(m_cursor.value());}
    DirectoryObjType getValueType() const { return ByteStringOps::getType(m_cursor.value()); }
    std::string getValueTypeName() const { return ByteStringOps::getTypeName(m_cursor.value()); }
    constexpr explicit operator bool() const noexcept { return m_cursor.operator bool(); }

private:
    BTree::Cursor m_cursor;
};

//////////////////////////////////////////////////////////////////////////

inline DirectoryStructure::Cursor DirectoryStructure::find(const DirectoryKey& dkey) const
{
    return m_btree.find(dkey.asByteStringView());
}

inline DirectoryStructure::Cursor DirectoryStructure::begin(const DirectoryKey& dkey) const
{
    return m_btree.begin(dkey.asByteStringView());
}

inline DirectoryStructure::Cursor DirectoryStructure::next(Cursor cursor) const 
{ 
    return m_btree.next(cursor.m_cursor); 
}




}
