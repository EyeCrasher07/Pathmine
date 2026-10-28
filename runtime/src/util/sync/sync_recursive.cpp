#include "sync_recursive.h"
#include "sync_spinlock.h"

// NV-DXVK start: Native thread ownership
#ifndef _WIN32
#include <sys/syscall.h>
#include <unistd.h>
#endif
// NV-DXVK end

namespace dxvk::sync {

  void RecursiveSpinlock::lock() {
    spin(2000, [this] { return try_lock(); });
  }


  void RecursiveSpinlock::unlock() {
    if (likely(m_counter == 0))
      m_owner.store(0, std::memory_order_release);
    else
      m_counter -= 1;
  }


  bool RecursiveSpinlock::try_lock() {
    // NV-DXVK start: Native thread ownership
#ifdef _WIN32
    uint32_t threadId = GetCurrentThreadId();
#else
    const uint32_t threadId = static_cast<uint32_t>(::syscall(SYS_gettid));
#endif
    // NV-DXVK end
    uint32_t expected = 0;

    bool status = m_owner.compare_exchange_weak(
      expected, threadId, std::memory_order_acquire);
    
    if (status)
      return true;
    
    if (expected != threadId)
      return false;
    
    m_counter += 1;
    return true;
  }

}
