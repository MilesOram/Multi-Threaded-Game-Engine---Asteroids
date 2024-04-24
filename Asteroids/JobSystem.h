#pragma once
#include "Top.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <queue>

class Gamestate;

// didn't want the job system to be tightly coupled to the singleton Gamestate, decided to use a namespace - seems like it exists above everything
namespace JobSystem
{
    /*
    * effective members - in .cpp
    * 
    std::queue<Declaration> g_JobQueue;
    std::queue<Declaration> g_JobBufferQueue;
    std::queue<Declaration> g_JobDelayedBufferQueue;
    std::vector<Declaration> g_UpkeepJobs;

    std::mutex g_JobMutex;
    std::mutex g_BufferMutex;
    std::mutex g_DelayedBufferMutex;
    std::mutex g_UpkeepMutex;
    std::condition_variable g_JobCV;
    bool g_Ready = false;
    std::atomic<bool> g_Shutdown{ false };
    std::atomic<int> g_UpkeepJobIndex{ 0 };

    */

    // allow use of member functions of other classes as jobs
    struct MemberFunctionWrapper
    {
        void* instance;
        void (*func)(void*, uintptr_t);
    };
    template< typename T, void (T::* MemberFunction)(uintptr_t) >
    void MemberFunctionDispatcher(void* instance, uintptr_t param);

    // allowable priorities - not currently used much
    enum class Priority { LOW, NORMAL, HIGH, CRITICAL };

    // counter - decremented when job is finished, can be shared between multiple jobs
    struct Counter 
    {
        std::atomic<int> count;
    };
    // currently only use a handful of counters in total which persist, but in theory this could use some custom allocator instead of using 'new'
    Counter* AllocCounter();
    void FreeCounter(Counter* pCounter);

    // simple job declaration - contains all info necessary to execute a job
    struct Declaration 
    {
        // wraps the desired member function - function is required to take uintptr_t as its only argument
        MemberFunctionWrapper m_MemberFunction;
        // stores data for job, can either directly store info or stores a pointer to some job data, destination function static_casts to the type it needs
        // works on assumption the data is prepared correctly
        uintptr_t m_Param;
        // high prio jobs are completed first
        Priority m_Priority;
        // pointer to counter which is decremented on job completion, used for synchronisation
        Counter* m_pCounter;
    };
    // used to sync with main thread
    bool IsBufferEmpty();
    // called after game ended as part of system shutdown
    void ClearBuffer();
    // kick job(s), takes a decl and adds it to the job queue, then notifies thread(s)
    void KickJob(const Declaration& decl);
    void KickJobs(int count, const Declaration aDecl[]);
    // buffer queue stores the jobs to be executed in the next phase
    void AddJobToBuffer(const Declaration& decl);
    void AddJobsToBuffer(int count, const Declaration aDecl[]);
    void AddJobsToBuffer(const std::vector<Declaration>& vDecl);
    // temporary solution to allow object removal job to be created during the update phase and be executed during cleanup (update->collision->cleanup)
    void AddJobToDelayedBuffer(const Declaration& decl);
    // always active jobs
    void AddJobToUpkeep(const Declaration& decl);
    // wait until a counter is 0 and all jobs using that counter have completed
    void WaitForCounter(Counter* pCounter);
    // used for phase transition, waits for current phase to be done, then swaps buffer and main job queues, and signals (also moves delayed buffer->buffer)
    void WaitForCounterAndSwapBuffers(Counter* pCounter, bool waitForMainThread = false);
    // add job(s) to job queue and wait for it/them to finish 
    void KickJobAndWait(const Declaration& decl);
    void KickJobsAndWait(int count, const Declaration aDecl[]);
    // loop function for all threads, stay in this function until shutdown, locks, checks queue for job and executes if there, otherwise waits for cv signal
    void JobWorkerThread();
    // start
    void InitJobSystem(int numWorkerThreads);
    // stop
    void ShutdownJobSystem();
};