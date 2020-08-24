
#pragma once

#include "Lock.h"
#include <optional>
#include <variant>
#include <mutex>
#include <shared_mutex>

namespace TxFs
{

/// Implements the lock-protocol.
template <typename TSharedMutex, typename TMutex>
class LockProtocol final
{
public:
    Lock readAccess();
    std::optional<Lock> tryReadAccess();

    Lock writeAccess();
    std::optional<Lock> tryWriteAccess();

    CommitLock commitAccess(Lock&& writeLock);
    std::variant<CommitLock, Lock> tryCommitAccess(Lock&& writeLock);

private:
    TSharedMutex m_signal;
    TSharedMutex m_shared;
    TMutex m_writer;
};

///////////////////////////////////////////////////////////////////////////

template <typename TSMutex, typename TXMutex>
inline Lock LockProtocol<TSMutex, TXMutex>::readAccess()
{
    std::shared_lock slock(m_signal);
    m_shared.lock_shared();
    return Lock(&m_shared, [](void* m) { static_cast<TSMutex*>(m)->unlock_shared(); });
}

template <typename TSMutex, typename TXMutex>
inline std::optional<Lock> LockProtocol<TSMutex, TXMutex>::tryReadAccess()
{
    std::shared_lock slock(m_signal, std::try_to_lock);
    if (!slock)
        return std::nullopt;

    if (!m_shared.try_lock_shared())
        return std::nullopt;

    return Lock(&m_shared, [](void* m) { static_cast<TSMutex*>(m)->unlock_shared(); });
}

template <typename TSMutex, typename TXMutex>
inline Lock LockProtocol<TSMutex, TXMutex>::writeAccess()
{
    m_writer.lock();
    return Lock(&m_writer, [](void* m) { static_cast<TXMutex*>(m)->unlock(); });
}

template <typename TSMutex, typename TXMutex>
inline std::optional<Lock> LockProtocol<TSMutex, TXMutex>::tryWriteAccess()
{
    if (!m_writer.try_lock())
        return std::nullopt;

    return Lock(&m_writer, [](void* m) { static_cast<TXMutex*>(m)->unlock(); });
}

template <typename TSMutex, typename TXMutex>
CommitLock LockProtocol<TSMutex, TXMutex>::commitAccess(Lock&& writeLock)
{
    if (!writeLock.isSameMutex(&m_writer))
        throw std::runtime_error("Incompatible writeLock parameter for commitAccess()");

    std::unique_lock ulock(m_signal);

    m_shared.lock();
    return CommitLock(Lock(&m_shared, [](void* m) { static_cast<TSMutex*>(m)->unlock(); }), std::move(writeLock));
}

template <typename TSMutex, typename TXMutex>
std::variant<CommitLock, Lock> LockProtocol<TSMutex, TXMutex>::tryCommitAccess(Lock&& writeLock)
{
    if (!writeLock.isSameMutex(&m_writer))
        throw std::runtime_error("Incompatible writeLock parameter for commitAccess()");

    std::unique_lock ulock(m_signal, std::try_to_lock);
    if (!ulock)
        return std::move(writeLock);

    if (!m_shared.try_lock())
        return std::move(writeLock);
    
    return CommitLock(Lock(&m_shared, [](void* m) { static_cast<TSMutex*>(m)->unlock(); }), std::move(writeLock));
}

}
