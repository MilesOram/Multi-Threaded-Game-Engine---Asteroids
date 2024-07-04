#pragma once
#include "Top.h"
#include <atomic>
#include <memory>
#include <unordered_map>
#include <mutex>

class GameObject;

// thread-safe lock-free object pool, uses linked list with atomic removal and insertion at the head, allows dynamic pool size adjustments based on config params
class ObjectPool
{
public:
    ObjectPool() = delete;
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = delete;
    ObjectPool& operator=(ObjectPool&&) noexcept = delete;

    // pool must be created with a prefab, and optional args for how the pool should function and be maintained
    ObjectPool(const std::shared_ptr<GameObject>& prefab, int countIncreasePerExpansion = 3, int initialAllocationCount = 10, float lowerBoundPC = 0.2f, float upperBoundPC = 0.5f);
    
    // objects in/out - thread-safe, lock-free insertion and removal
    std::shared_ptr<GameObject> GetPooledObject();
    void AddToPool(std::shared_ptr<GameObject>& Object);
    
    // upkeep job
    void MaintainPoolBuffer();
    
    // change bounds at runtime in anticipation of higher or lower requirements for the forseeable future
    void SetPoolSizeBoundPercentages(float lower, float upper);
    void FillPool(int count);
private:
    // node stores object and sharedptr to next - using sharedptr avoids any ABA problems with reallocation
    struct Node
    {
        std::shared_ptr<GameObject> Object;
        std::shared_ptr<Node> Next;
        Node(std::shared_ptr<GameObject> _Object, std::shared_ptr<Node> _Next) : Object(_Object), Next(_Next) {}
    };
    
    // whenever the pool is expanded, copy the prefab to make new objects (T must derive from GameObject)
    std::shared_ptr<GameObject> m_Prefab;
    
    // head of linked list storing all inactive objects - must ONLY be used as if it's atomic
    std::shared_ptr<Node> m_Head;
    
    // atomic counts to keep track for maintaining pool size
    std::atomic<int> m_TotalObjectCount;
    std::atomic<int> m_CurrentPoolSize;
    
    // pool config settings
    const int m_CountIncreasePerExpansion = 3;
    const int m_MinBufferSize = 5;
    float m_PoolSizeUpperBoundPC = .1f;
    float m_PoolSizeLowerBoundPC = .2f;

    void RemoveHead();
};

// manages all pools, keyed by a string (for readability sake, could easily be keyed by an int/enum class)
class ObjectPoolManager
{
private:
    typedef std::unordered_map<std::string, std::unique_ptr<ObjectPool>> MapOfPools;
    MapOfPools m_Pools;
    std::mutex m_MaintenanceMutex;
    MapOfPools::iterator m_PoolIterator;
public:
    ObjectPool* CreatePool(const std::string& poolName, const std::shared_ptr<GameObject>& prefab, int countIncreasePerExpansion, int initialAllocationCount, float lowerBoundPC = 0.2f, float upperBoundPC = 0.5f);

    std::shared_ptr<GameObject> GetPooledObject(const std::string& PoolName);
    void ReturnToPool(std::shared_ptr<GameObject> obj, const std::string& poolName);
    void MaintainPoolBuffers(uintptr_t _unused);
};

