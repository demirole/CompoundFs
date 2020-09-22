
#pragma once

#include "Interval.h"



namespace TxFs
{
class Lock;
class CommitLock;

enum class OpenMode { Create, Open, ReadOnly };

class FileInterface
{
public:
    virtual ~FileInterface() = default;

    virtual Interval newInterval(size_t maxPages) = 0;
    virtual const uint8_t* writePage(PageIndex id, size_t pageOffset, const uint8_t* begin, const uint8_t* end) = 0;
    virtual const uint8_t* writePages(Interval iv, const uint8_t* page) = 0;
    virtual uint8_t* readPage(PageIndex id, size_t pageOffset, uint8_t* begin, uint8_t* end) const = 0;
    virtual uint8_t* readPages(Interval iv, uint8_t* page) const = 0;
    virtual size_t currentSize() const = 0; // file size in number of pages
    virtual void flushFile() = 0;
    virtual void truncate(size_t numberOfPages) = 0;

    virtual Lock defaultAccess() = 0;
    virtual Lock readAccess() = 0;
    virtual Lock writeAccess() = 0;
    virtual CommitLock commitAccess(Lock&& writeLock) = 0;
};



}
