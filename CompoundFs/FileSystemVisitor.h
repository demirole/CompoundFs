
#pragma once

#include "FileSystem.h"
#include "Path.h"
#include "SmallBufferStack.h"
#include "TreeValue.h"
#include <vector>
#include <string>
#include <string_view>

namespace TxFs
{

enum class VisitorControl { Continue, Break };

class FileSystemVisitor
{
    FileSystem& m_fs;

public:
    FileSystemVisitor(FileSystem& fs)
        : m_fs(fs)
    {
    }

    template <typename TVisitor>
    void visit(Path path, TVisitor& visitor)
    {
        FileSystem::Cursor cursor = prepareVisit(path, visitor);
        SmallBufferStack<FileSystem::Cursor, 10> stack;
        while (cursor)
        {
            if (visitor(Path(cursor.key().first, cursor.key().second), cursor.value()) == VisitorControl::Break)
                return;

            auto type = cursor.value().getType();

            if (type == TreeValue::Type::Folder)
            {
                stack.push(m_fs.next(cursor));
                cursor = m_fs.begin(Path(cursor.value().toValue<Folder>(), ""));
            }
            else
                cursor = m_fs.next(cursor);

            while (!cursor && !stack.empty())
            {
                cursor = stack.top();
                stack.pop();
            }
        }
    }

private:
    template <typename TVisitor>
    FileSystem::Cursor prepareVisit(Path path, TVisitor& visitor)
    {
        // There is no root entry - handle it...
        if (path == RootPath)
        {
            auto control = visitor(path, TreeValue { Folder { path.RootFolder } });
            return control == VisitorControl::Continue ? m_fs.begin(path) : FileSystem::Cursor();
        }

        FileSystem::Cursor cursor = m_fs.find(path);
        if (!cursor)
            return cursor;

        auto control = visitor(Path(cursor.key().first, cursor.key().second), cursor.value());
        if (control == VisitorControl::Continue && cursor.value().getType() == TreeValue::Type::Folder)
            return m_fs.begin(Path(cursor.value().toValue<Folder>(), ""));

        return FileSystem::Cursor();
    }
};

///////////////////////////////////////////////////////////////////////////////

class FolderKey
{
    Folder m_folder;
    std::string m_name;

public:
    FolderKey(std::pair<Folder, std::string_view> key)
        : m_folder(key.first)
        , m_name(key.second)
    {
    }

    operator Path() const { return Path(m_folder, m_name); }
    std::string_view name() const { return m_name; }
};

//////////////////////////////////////////////////////////////////////////

struct TreeEntry
{
    FolderKey m_key;
    TreeValue m_value;
};

//////////////////////////////////////////////////////////////////////////

class FsCompareVisitor
{
public:
    enum class Result { NotFound, NotEqual, Equal };

private:
    FileSystem& m_sourceFs;
    FileSystem& m_destFs;
    Folder m_folder;
    std::string m_name;
    Result m_result;
    SmallBufferStack<std::pair<Folder, Folder>, 10> m_stack;
    std::unique_ptr<char[]> m_buffer;
    static constexpr size_t BufferSize = 32 * 4096;

public:
    FsCompareVisitor(FileSystem& sourceFs, FileSystem& destFs, Path path)
        : m_sourceFs(sourceFs)
        , m_destFs(destFs)
        , m_folder { path.m_parent}
        , m_name{path.m_relativePath}
        , m_result(Result::Equal)
    {
    }

    VisitorControl operator()(Path path, const TreeValue& value);

    Result result() const { return m_result; }

private:
    Path getDestPath(Path sourcePath);
    std::optional<TreeValue> getDestValue(Path destPath);
    VisitorControl dispatch(Path sourcePath, const TreeValue& sourceValue, Path destPath);
    VisitorControl compareFiles(Path sourcePath, Path destPath);
    char* getLazyMemoryBuffer();    
    VisitorControl compareFiles(ReadHandle sourceHandle, ReadHandle destHandle);
};

///////////////////////////////////////////////////////////////////////////////



}

