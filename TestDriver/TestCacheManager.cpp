

#include "Test.h"
#include "../CompoundFs/MemoryFile.h"
#include "../CompoundFs/CacheManager.h"
#include "../CompoundFs/CommitHandler.h"
#include <algorithm>

using namespace TxFs;

namespace
{
    uint8_t readByte(const RawFileInterface* rfi, PageIndex idx)
    {
        uint8_t res;
        rfi->readPage(idx, 0, &res, &res + 1);
        return res;
    }

    void writeByte(RawFileInterface* rfi, PageIndex idx, uint8_t val)
    {
        rfi->writePage(idx, 0, &val, &val + 1);
    }
    }

///////////////////////////////////////////////////////////////////////////////


TEST(CacheManager, newPageIsCachedButNotWritten)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    PageIndex idx;
    {
        auto p = cm.newPage();
        idx = p.m_index;
        {
            auto p2 = cm.loadPage(p.m_index);
            CHECK(p2 == p);
            *p.m_page = 0xaa;
        }
    }
    auto p2 = cm.loadPage(idx);
    CHECK(*p2.m_page == 0xaa);
    CHECK(readByte(cm.getRawFileInterface(), idx) != *p2.m_page);
}

TEST(CacheManager, loadPageIsCachedButNotWritten)
{
    auto memFile = std::make_unique<MemoryFile>();
    auto id = memFile->newInterval(1).begin();
    writeByte(memFile.get(), id, 42);

    CacheManager cm(std::move(memFile));
    auto p = cm.loadPage(id);
    auto p2 = cm.loadPage(id);
    CHECK(p == p2);

    *std::const_pointer_cast<uint8_t>(p.m_page) = 99;
    CHECK(readByte(cm.getRawFileInterface(), id) == 42);
}

TEST(CacheManager, trimReducesSizeOfCache)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    for (int i = 0; i < 10; i++)
        auto page = cm.newPage();

    CHECK(cm.trim(20) == 10);
    CHECK(cm.trim(9) == 9);
    CHECK(cm.trim(5) == 5);
    CHECK(cm.trim(0) == 0);
}

TEST(CacheManager, newPageGetsWrittenToFileOnTrim)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.newPage().m_page;
        *p = i + 1;
    }

    cm.trim(0);
    for (int i = 0; i < 10; i++)
        CHECK(readByte(cm.getRawFileInterface(), i) == i + 1);
}

TEST(CacheManager, pinnedPageDoNotGetWrittenToFileOnTrim)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.newPage().m_page;
        *p = i + 1;
    }
    auto p1 = cm.loadPage(0);
    auto p2 = cm.loadPage(9);

    CHECK(cm.trim(0) == 2);

    for (int i = 1; i < 9; i++)
        CHECK(readByte(cm.getRawFileInterface(), i) == i + 1);

    CHECK(readByte(cm.getRawFileInterface(), 0) != *p1.m_page);
    CHECK(readByte(cm.getRawFileInterface(), 9) != *p2.m_page);
}

TEST(CacheManager, newPageGetsWrittenToFileOn2TrimOps)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.newPage().m_page;
        *p = i + 1;
    }

    cm.trim(0);

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.makePageWritable(cm.loadPage(i));
        *p.m_page = i + 10;
    }

    cm.trim(0);

    for (int i = 0; i < 10; i++)
        CHECK(readByte(cm.getRawFileInterface(), i) == i + 10);
}

TEST(CacheManager, newPageDontGetWrittenToFileOn2TrimOpsWithoutSettingDirty)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.newPage().m_page;
        *p = i + 1;
    }

    cm.trim(0);

    for (int i = 0; i < 10; i++)
    {
        auto p = std::const_pointer_cast<uint8_t>(cm.loadPage(i).m_page); // don't do that! !!!!!!!!!!!!!!!
        *p = i + 10;                                                      // change page but no pageDirty()
    }

    cm.trim(0);

    for (int i = 0; i < 10; i++)
        CHECK(readByte(cm.getRawFileInterface(), i) == i + 1);
}

TEST(CacheManager, dirtyPagesCanBeEvictedAndReadInAgain)
{
    std::unique_ptr<RawFileInterface> file = std::make_unique<MemoryFile>();
    {
        CacheManager cm(std::move(file));
        for (int i = 0; i < 10; i++)
        {
            auto p = cm.newPage().m_page;
            *p = i + 1;
        }
        cm.trim(0);
        file = cm.handOverFile();
    }

    CacheManager cm(std::move(file));
    for (int i = 0; i < 10; i++)
    {
        auto p = cm.makePageWritable(cm.loadPage(i)).m_page;
        *p = i + 10;
    }
    cm.trim(0);

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.loadPage(i).m_page;
        CHECK(*p == i + 10);
    }
}

TEST(CacheManager, dirtyPagesCanBeEvictedTwiceAndReadInAgain)
{
    std::unique_ptr<RawFileInterface> file = std::make_unique<MemoryFile>();
    {
        CacheManager cm(std::move(file));
        for (int i = 0; i < 10; i++)
        {
            auto p = cm.newPage().m_page;
            *p = i + 1;
        }
        cm.trim(0);
        file = cm.handOverFile();
    }

    CacheManager cm(std::move(file));
    for (int i = 0; i < 10; i++)
    {
        auto p = cm.makePageWritable(cm.loadPage(i)).m_page;
        *p = i + 10;
    }
    cm.trim(0);

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.makePageWritable(cm.loadPage(i)).m_page;
        *p = i + 20;
    }
    cm.trim(0);
    CHECK(cm.getRawFileInterface()->currentSize() == 20);

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.loadPage(i).m_page;
        CHECK(*p == i + 20);
    }
}

TEST(CacheManager, dirtyPagesGetDiverted)
{
    std::unique_ptr<RawFileInterface> file = std::make_unique<MemoryFile>();
    {
        CacheManager cm(std::move(file));
        for (int i = 0; i < 10; i++)
        {
            auto p = cm.newPage().m_page;
            *p = i + 1;
        }
        cm.trim(0);
        file = cm.handOverFile();
    }

    CacheManager cm(std::move(file));
    for (int i = 0; i < 10; i++)
    {
        auto p = cm.makePageWritable(cm.loadPage(i)).m_page;
        *p = i + 10;
    }
    cm.trim(0);
    auto divertedPageIds = cm.buildCommitHandler().getDivertedPageIds();
    CHECK(divertedPageIds.size() == 10);
    for (auto page : divertedPageIds)
        CHECK(page >= 10);
}

TEST(CacheManager, repurposedPagesCanComeFromCache)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.newPage().m_page;
        *p = i + 1;
    }

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.repurpose(i).m_page;
        CHECK(*p == i + 1);
    }
}

TEST(CacheManager, repurposedPagesAreNotLoadedIfNotInCache)
{
    CacheManager cm(std::make_unique<MemoryFile>());

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.newPage().m_page;
        *p = i + 1;
    }
    cm.trim(0);

    // make sure the PageAllocator has pages with different values
    for (int i = 0; i < 10; i++)
    {
        auto p = cm.newPage().m_page;
        *p = i + 100;
    }

    for (int i = 0; i < 10; i++)
    {
        auto p = cm.repurpose(i).m_page;
        CHECK(*p != i + 1);
    }
}

TEST(CacheManager, setPageIntervalAllocator)
{
    CacheManager cm(std::make_unique<MemoryFile>());
    cm.setPageIntervalAllocator([](size_t) { return Interval(5); });
    auto pdef = cm.newPage();
    pdef.m_page.reset();

    try
    {
        cm.trim(0);
        CHECK(false);
    }
    catch (std::exception&)
    {}
}

TEST(CacheManager, NoLogsReturnEmpty)
{
    CacheManager cm(std::make_unique<MemoryFile>());
    CHECK(cm.readLogs().empty());

    cm.newPage();
    CHECK(cm.readLogs().empty());
}

TEST(CacheManager, ReadLogsReturnTheLogs)
{
    CacheManager cm(std::make_unique<MemoryFile>());
    cm.newPage();
    std::vector<std::pair<PageIndex, PageIndex>> logs(1000);
    std::generate(logs.begin(), logs.end(), [n = 0]() mutable { return std::make_pair(n++, n); });
    auto ch = cm.buildCommitHandler();
    ch.writeLogs(logs);
    auto logs2 = cm.readLogs();
    std::sort(logs2.begin(), logs2.end());
    CHECK(logs == logs2);
}
