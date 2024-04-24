#include "JobSystem.h"
#include "Gamestate.h"
#include "ObjectPool.h"
#include "CollisionGrid.h"

namespace JobSystem
{
    // declare templates
    template void MemberFunctionDispatcher<ObjectPoolManager, &ObjectPoolManager::MaintainPoolBuffers>(void*, uintptr_t);
    template void MemberFunctionDispatcher<Gamestate, &Gamestate::Update>(void*, uintptr_t);
    template void MemberFunctionDispatcher<Gamestate, &Gamestate::UpdateGameObjectSection>(void*, uintptr_t);
    template void MemberFunctionDispatcher<Gamestate, &Gamestate::ManageThreadPhaseTransition>(void*, uintptr_t);
    template void MemberFunctionDispatcher<Gamestate, &Gamestate::CreateSnapshotForGameObjectSection>(void*, uintptr_t);
    template void MemberFunctionDispatcher<Gamestate, &Gamestate::ProcessInactiveObjects>(void*, uintptr_t);
    template void MemberFunctionDispatcher<ObjectCollisionGrid, &ObjectCollisionGrid::ResolveCollisionsOfCells>(void*, uintptr_t);
    template void MemberFunctionDispatcher<ObjectCollisionGrid, &ObjectCollisionGrid::ClearFrameCollisionPairs>(void*, uintptr_t);
    // job queue
    std::queue<Declaration> g_JobQueue;
    std::queue<Declaration> g_JobBufferQueue;
    std::queue<Declaration> g_JobDelayedBufferQueue;
    std::vector<Declaration> g_UpkeepJobs;

    // synchronization primitives
    std::mutex g_JobMutex;
    std::mutex g_BufferMutex;
    std::mutex g_DelayedBufferMutex;
    std::mutex g_UpkeepMutex;
    std::condition_variable g_JobCV;
    bool g_Ready = false;
    std::atomic<bool> g_Shutdown{ false };
    std::atomic<int> g_UpkeepJobIndex{ 0 };

    template< typename T, void (T::* MemberFunction)(uintptr_t) >
    void MemberFunctionDispatcher(void* instance, uintptr_t param)
    {
        (static_cast<T*>(instance)->*MemberFunction)(param);
    }
    bool IsBufferEmpty()
    {
        {
            std::lock_guard<std::mutex> lock(g_BufferMutex);
            return g_JobBufferQueue.size() == 0;
        }
    }
    void ClearBuffer()
    {
        while (!g_JobBufferQueue.empty()) g_JobBufferQueue.pop();
    }

    // worker threads
    std::vector<std::thread> g_workerThreads;

    Counter* AllocCounter() 
    { 
        return new Counter{ 0 };
    }
    void FreeCounter(Counter* pCounter) 
    {
        delete pCounter; 
    }

    void KickJob(const Declaration& decl)
    {
        {
            std::lock_guard<std::mutex> lock(g_JobMutex);
            g_JobQueue.push(decl);
            g_Ready = true;
        }
        g_JobCV.notify_one();
    }
    void KickJobs(int count, const Declaration aDecl[])
    {
        {
            std::lock_guard<std::mutex> lock(g_JobMutex);
            for (int i = 0; i < count; ++i)
            {
                g_JobQueue.push(aDecl[i]);
            }
            g_Ready = true;
        }
        g_JobCV.notify_all();
    }
    void AddJobToBuffer(const Declaration& decl)
    {
        {
            std::lock_guard<std::mutex> lock(g_BufferMutex);
            g_JobBufferQueue.push(decl);
        }
    }
    void AddJobsToBuffer(int count, const Declaration aDecl[])
    {
        {
            std::lock_guard<std::mutex> lock(g_BufferMutex);
            for (int i = 0; i < count; ++i)
            {
                g_JobBufferQueue.push(aDecl[i]);
            }
        }
    }
    void AddJobsToBuffer(const std::vector<Declaration>& vDecl)
    {
        {
            std::lock_guard<std::mutex> lock(g_BufferMutex);
            for (size_t i = 0; i < vDecl.size(); ++i)
            {
                g_JobBufferQueue.push(vDecl[i]);
            }
        }
    }
    void AddJobToDelayedBuffer(const Declaration& decl)
    {
        {
            std::lock_guard<std::mutex> lock(g_DelayedBufferMutex);
            g_JobDelayedBufferQueue.push(decl);
        }
    }

    void AddJobToUpkeep(const Declaration& decl)
    {
        {
            std::lock_guard<std::mutex> lock(g_UpkeepMutex);
            g_UpkeepJobs.push_back(decl);
        }
    }

    // wait for job to terminate (for its Counter to become zero)
    void WaitForCounter(Counter* pCounter)
    {
        while (pCounter->count.load() > 0)
        {
            std::this_thread::yield();
        }
    }
    // wait for job to terminate and then swap queues to initiate next phase
    void WaitForCounterAndSwapBuffers(Counter* pCounter, bool waitForMainThread)
    {
        while (pCounter->count.load() > 0)
        {
            std::this_thread::yield();
        }
        {
            std::unique_lock<std::mutex> lock(g_BufferMutex);
            std::unique_lock<std::mutex> lock2(g_JobMutex);
            std::unique_lock<std::mutex> lock3(g_DelayedBufferMutex);
            // swap buffer queue with main job queue, then move any leftover jobs from the main queue back over
            std::swap(g_JobBufferQueue, g_JobQueue);
            // -1 since the transition job doesn't use this counter, but it is always present in the bufferqueue
            pCounter->count.store(g_JobQueue.size() + waitForMainThread - 1);
            while (!g_JobBufferQueue.empty())
            {
                g_JobQueue.push(g_JobBufferQueue.front());
                g_JobBufferQueue.pop();
            }
            if (!g_JobDelayedBufferQueue.empty())
            {
                std::swap(g_JobBufferQueue, g_JobDelayedBufferQueue);
            }
        }
        g_Ready = true;
        g_JobCV.notify_all();
    }

    // kick jobs and wait for completion
    void KickJobAndWait(const Declaration& decl)
    {
        KickJob(decl);
        WaitForCounter(decl.m_pCounter);
    }

    void KickJobsAndWait(int count, const Declaration aDecl[])
    {
        for (int i = 0; i < count; ++i)
        {
            KickJob(aDecl[i]);
        }
        for (int i = 0; i < count; ++i)
        {
            WaitForCounter(aDecl[i].m_pCounter);
        }
    }

    void JobWorkerThread()
    {
        ThreadIndex = Gamestate::instance->ObtainUniqueThreadLocalIndex();
        while (true)
        {
            Declaration declCopy;
            {
                std::unique_lock<std::mutex> lock(g_JobMutex);
                g_JobCV.wait(lock, [] { return g_Ready || g_Shutdown; });
                if (g_Shutdown && g_JobQueue.empty())
                {
                    // If shutdown is requested and the job queue is empty, exit the thread
                    break;
                }
                // Try to get the next job from the queue
                if (g_Ready)
                {
                    if (g_JobQueue.empty())
                    {
                        g_Ready = false;
                        continue;
                    }
                    declCopy = g_JobQueue.front();
                    g_JobQueue.pop();
                }
                else
                {
                    continue;
                }
            }
            // complete the job the decrement the counter
            declCopy.m_MemberFunction.func(declCopy.m_MemberFunction.instance, declCopy.m_Param);
            declCopy.m_pCounter->count.fetch_sub(1);
            // Check for low-priority jobs
            if (ThreadIndex == NUM_THREADS-1)
            {
                while (g_JobQueue.empty() && !g_UpkeepJobs.empty() && !g_Shutdown)
                {
                    g_UpkeepMutex.lock();
                    size_t index = g_UpkeepJobIndex.fetch_add(1) % g_UpkeepJobs.size();
                    Declaration upkeepDecl = g_UpkeepJobs[index];
                    g_UpkeepMutex.unlock();

                    upkeepDecl.m_MemberFunction.func(upkeepDecl.m_MemberFunction.instance, upkeepDecl.m_Param);
                    // no decrement counter since these are always active
                    std::this_thread::yield();
                }
            }
        }
    }

    void InitJobSystem(int numWorkerThreads)
    {
        g_workerThreads.reserve(numWorkerThreads);
        for (int i = 0; i < numWorkerThreads; ++i)
        {
            g_workerThreads.emplace_back(JobWorkerThread);
        }
    }

    void ShutdownJobSystem()
    {
        g_Shutdown = true;

        // Notify all worker threads to wake up
        {
            std::lock_guard<std::mutex> lock(g_JobMutex);
            g_Ready = true;
        }
        g_JobCV.notify_all();

        // Wait for all worker threads to finish
        for (auto& thread : g_workerThreads)
        {
            thread.join();
        }

        // Clear the worker threads vector
        g_workerThreads.clear();
    }
};