
#include "CacheManager.h"
#include "RawFileInterface.h"
#include "TypedCacheManager.h"
#include "LogPage.h"

#include <assert.h>
#include <algorithm>
#include <tuple>

using namespace TxFs;

CacheManager::CacheManager(RawFileInterface* rfi, uint32_t maxPages) noexcept
    : m_rawFileInterface(rfi)
    , m_pageMemoryAllocator(maxPages)
    , m_maxCachedPages(maxPages)
{}

/// Delivers a new page. The page is either allocated form the FreeStore or it comes from extending the file. The
/// PageDef<> is writable as it is expected that a new page was requested because you want to write to it.
PageDef<uint8_t> CacheManager::newPage()
{
    auto page = m_pageMemoryAllocator.allocate();
    auto id = newPageIndex();
    m_cache.emplace(id, CachedPage(page, PageMetaData::New));
    m_newPageSet.insert(id);
    trimCheck();
    return PageDef<uint8_t>(page, id);
}

/// Loads the specified page. The page is loaded because previous transactions left state that is now being accessed.
/// The returnvalue can be transformed into something writable (makePageWritable()) which in turn makes this page
/// subject to the dirty-page-protocol.
ConstPageDef<uint8_t> CacheManager::loadPage(PageIndex origId)
{
    auto id = redirectPage(origId);
    auto it = m_cache.find(id);
    if (it == m_cache.end())
    {
        auto page = m_pageMemoryAllocator.allocate();
        m_rawFileInterface->readPage(id, 0, page.get(), page.get() + 4096);
        m_cache.emplace(id, CachedPage(page, PageMetaData::Read));
        trimCheck();
        return ConstPageDef<uint8_t>(page, origId);
    }

    it->second.m_usageCount++;
    return ConstPageDef<uint8_t>(it->second.m_page, origId);
}

/// Reuses a page for new purposes. It works like loadPage() without physically loading the page, followed by
/// setPageDirty(). The page is treated as PageMetaData::New if we find the page in the m_newPageSet otherwise it will
/// be flagged as PageMetaData::Dirty. Note: Do not feed regular FreeStore pages to this API as they wrongly end up
/// following the dirty-page-protocol.
PageDef<uint8_t> CacheManager::repurpose(PageIndex origId)
{
    auto id = redirectPage(origId);
    int type = m_newPageSet.count(id) ? PageMetaData::New : PageMetaData::Dirty;
    auto it = m_cache.find(id);
    if (it == m_cache.end())
    {
        auto page = m_pageMemoryAllocator.allocate();
        m_cache.emplace(id, CachedPage(page, type));
        trimCheck();
        return PageDef<uint8_t>(page, origId);
    }

    it->second.m_usageCount++;
    it->second.m_type = type;
    return PageDef<uint8_t>(it->second.m_page, origId);
}

/// Transforms a const page into a writable page. CacheManager needs to know that pages written by a previous
/// transaction are now about to be changed. Such pages are subject to the DirtyPages protocol.
PageDef<uint8_t> CacheManager::makePageWritable(const ConstPageDef<uint8_t>& loadedPage) noexcept
{
    setPageDirty(loadedPage.m_index);
    return PageDef<uint8_t>(std::const_pointer_cast<uint8_t>(loadedPage.m_page), loadedPage.m_index);
}

/// Marks that a page was changed: Pages previously read-in are marked dirty (which makes them follow the
/// Dirty-Page-Protocoll). All other pages are treated as PageMetaData::New.
void CacheManager::setPageDirty(PageIndex id) noexcept
{
    id = redirectPage(id);
    int type = m_newPageSet.count(id) ? PageMetaData::New : PageMetaData::Dirty;

    auto it = m_cache.find(id);
    assert(it != m_cache.end());
    it->second.m_type = type;
}

/// Finds out if a trim operation needs to be performed and does it if necessary.
void CacheManager::trimCheck() noexcept
{
    if (m_cache.size() > m_maxCachedPages)
        trim(m_maxCachedPages / 4 * 3);
}

// Trims down memory usage by maxPages. If users have a lot of pinned pages this is triggered too often. Make sure that
// there is sufficient space to deal with real-world scenarios.
size_t CacheManager::trim(uint32_t maxPages)
{
    auto prioritizedPages = getUnpinnedPages();
    maxPages = std::min(maxPages, (uint32_t) prioritizedPages.size());
    auto beginEvictSet = prioritizedPages.begin() + maxPages;

    std::nth_element(prioritizedPages.begin(), beginEvictSet, prioritizedPages.end());
    auto beginNewPageSet = std::partition(beginEvictSet, prioritizedPages.end(),
                                          [](PrioritizedPage psi) { return psi.m_type == PageMetaData::Dirty; });
    auto endNewPageSet = std::partition(beginNewPageSet, prioritizedPages.end(),
                                        [](PrioritizedPage psi) { return psi.m_type == PageMetaData::New; });

    evictDirtyPages(beginEvictSet, beginNewPageSet);
    evictNewPages(beginNewPageSet, endNewPageSet);
    removeFromCache(beginEvictSet, prioritizedPages.end());
    return m_cache.size();
}

/// Use installed allocation function or the rawFileInterface.
Interval CacheManager::allocatePageInterval(size_t maxPages) noexcept
{
    if (m_pageIntervalAllocator)
    {
        auto iv = m_pageIntervalAllocator(maxPages);
        if (iv.begin() == PageIdx::INVALID)
            m_pageIntervalAllocator = std::function<Interval(size_t)>();
        return iv;
    }
    return m_rawFileInterface->newInterval(maxPages);
}

/// Get the page-indices for redirected Dirty pages
std::vector<PageIndex> CacheManager::getRedirectedPages() const
{
    std::vector<PageIndex> pages;
    pages.reserve(m_redirectedPagesMap.size());
    for (const auto& [originalPage, redirectedPage]: m_redirectedPagesMap)
        pages.push_back(redirectedPage);

    return pages;
}

/// Find the page we moved the original page to or return identity.
PageIndex CacheManager::redirectPage(PageIndex id) const noexcept
{
    auto it = m_redirectedPagesMap.find(id);
    if (it == m_redirectedPagesMap.end())
        return id;
    return it->second;
}

/// Find all pages that are currently not pinned.
std::vector<CacheManager::PrioritizedPage> CacheManager::getUnpinnedPages() const
{
    std::vector<PrioritizedPage> pageSortItems;
    pageSortItems.reserve(m_cache.size());
    for (auto& cp: m_cache)
        if (cp.second.m_page.use_count() == 1) // we don't use weak_ptr => this is save
            pageSortItems.push_back(PrioritizedPage(cp.second, cp.first));
    return pageSortItems;
}

void CacheManager::evictDirtyPages(std::vector<PrioritizedPage>::iterator begin, std::vector<PrioritizedPage>::iterator end)
{
    for (auto it = begin; it != end; ++it)
    {
        assert(it->m_type == PageMetaData::Dirty);
        auto p = m_cache.find(it->m_id);
        assert(p != m_cache.end());
        auto id = newPageIndex();
        m_rawFileInterface->writePage(id, 0, p->second.m_page.get(), p->second.m_page.get() + 4096);
        m_redirectedPagesMap[it->m_id] = id;
        m_newPageSet.insert(id);
    }
}

void CacheManager::evictNewPages(std::vector<PrioritizedPage>::iterator begin, std::vector<PrioritizedPage>::iterator end)
{
    for (auto it = begin; it != end; ++it)
    {
        assert(it->m_type == PageMetaData::New);
        auto p = m_cache.find(it->m_id);
        assert(p != m_cache.end());
        m_rawFileInterface->writePage(p->first, 0, p->second.m_page.get(), p->second.m_page.get() + 4096);
    }
}

void CacheManager::removeFromCache(std::vector<PrioritizedPage>::iterator begin, std::vector<PrioritizedPage>::iterator end)
{
    for (auto it = begin; it != end; ++it)
        m_cache.erase(it->m_id);
}

void CacheManager::commit()
{
    // stop allocating from FreeStore
    m_pageIntervalAllocator = std::function<Interval(size_t)>();

    auto origToCopyPages = copyDirtyPages();
    m_rawFileInterface->commit();

    writeLogs(origToCopyPages);
    m_rawFileInterface->commit();
    
    // TODO: overwrite the original dirty pages
    // use the cache if its already there
    // otherwise just copy pages redirectIdx to origIdx
    // some New pages will have to be pushed to disk
    // but not the once we just used to overwrite the dirty pages!
    
    // cut the file
}

std::vector<std::pair<PageIndex, PageIndex>> CacheManager::copyDirtyPages()
{

    // TODO: add the Dirty pages from the cache to this
    std::vector<std::pair<PageIndex, PageIndex>> origToCopyPages;
    origToCopyPages.reserve(m_redirectedPagesMap.size());

    // stop redirecting loading
    std::unordered_map<PageIndex, PageIndex> redirectedPagesMap;
    redirectedPagesMap.swap(m_redirectedPagesMap);

    auto interval = m_rawFileInterface->newInterval(redirectedPagesMap.size());
    assert(interval.length() == redirectedPagesMap.size()); // here the file is just growing
    auto nextPage = interval.begin();

    for (const auto& [originalPageIdx, redirectedPageIdx]: redirectedPagesMap)
    {
        // TODO: don't loadPage()! avoid triggering a cache eviction. copy directly!
        auto pageDef = loadPage(originalPageIdx);
        m_rawFileInterface->writePage(nextPage, 0, pageDef.m_page.get(), pageDef.m_page.get() + 4096);
        origToCopyPages.emplace_back(originalPageIdx, nextPage++);
    }
    assert(nextPage == interval.end());

    // restore redirecting
    redirectedPagesMap.swap(m_redirectedPagesMap);
    return origToCopyPages;
}

void CacheManager::writeLogs(const std::vector<std::pair<PageIndex, PageIndex>>& origToCopyPages)
{
    auto begin = origToCopyPages.begin();
    while (begin != origToCopyPages.end())
    {
        auto pageIndex = m_rawFileInterface->newInterval(1).begin();
        LogPage logPage(pageIndex);
        begin = logPage.pushBack(begin, origToCopyPages.end());
        m_rawFileInterface->writePage(pageIndex, 0, (const uint8_t*) &logPage,
                                      ((const uint8_t*) &logPage) + sizeof(LogPage));
    }
}
