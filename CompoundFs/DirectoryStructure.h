
#pragma once

#include "CacheManager.h"
#include "FreeStore.h"
#include "BTree.h"
#include <memory>
#include <cstdint>

namespace TxFs
{

enum class Folder : uint32_t;

class DirectoryStructure
{
public:
    static constexpr Folder Root { 0 };

public:
    DirectoryStructure(const std::shared_ptr<CacheManager>& cacheManager, FileDescriptor freeStore,
                       PageIndex rootIndex = PageIdx::INVALID, uint32_t maxFolderId = 1);

    std::optional<Folder> makeSubFolder(std::string_view name, Folder folder = Root);
    std::optional<Folder> subFolder(std::string_view name, Folder folder = Root) const;
    size_t removeFolder(Folder folder);

    std::optional<FileDescriptor> createFile(std::string_view name, Folder folder = Root);
    std::optional<FileDescriptor> appendFile(std::string_view name, Folder folder = Root);

private:
    std::shared_ptr<CacheManager> m_cacheManager;
    BTree m_btree;
    uint32_t m_maxFolderId;
    FreeStore m_freeStore;
};
}
