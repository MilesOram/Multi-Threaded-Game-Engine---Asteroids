#include "ObjectPool.h"
#include "Gamestate.h"
#include "GameObject.h"
#include "PlayerShip.h"
#include "Asteroid.h"
#include "Projectile.h"
#include "Components.h"

ObjectPool::ObjectPool(const std::shared_ptr<GameObject>& prefab, int countIncreasePerExpansion, int initialAllocationCount, float lowerBoundPC, float upperBoundPC) :
    m_Prefab(prefab), m_CountIncreasePerExpansion(countIncreasePerExpansion), m_PoolSizeLowerBoundPC(lowerBoundPC), m_PoolSizeUpperBoundPC(upperBoundPC)
{
    m_Prefab->SetInactive();
    m_Prefab->GetComponent<PooledObjectComponent>()->SetPool(this);
    FillPool(initialAllocationCount);
}

std::shared_ptr<GameObject> ObjectPool::GetPooledObject()
{
    auto oldHead = std::atomic_load(&m_Head);
    do
    {
        // pool is empty - refill pool, then make new object to immediately return
        if (!oldHead)
        {
            FillPool(m_CountIncreasePerExpansion);
            auto newObj = m_Prefab->CloneToSharedPtr();
            m_TotalObjectCount.fetch_add(1);
            newObj->SetActive();
            return newObj;
        }
    } while (!std::atomic_compare_exchange_weak(&m_Head, &oldHead, oldHead->Next));

    // successful atomic exchange, take object from old head
    std::shared_ptr<GameObject> obj = oldHead->Object;
    m_CurrentPoolSize.fetch_sub(1);
    obj->SetActive();
    return oldHead->Object;
}

void ObjectPool::FillPool(int count)
{
    for (int i = 0; i < count; ++i)
    {
        auto newObj = m_Prefab->CloneToSharedPtr();
        AddToPool(newObj);
    }
    m_TotalObjectCount.fetch_add(count);
}

void ObjectPool::AddToPool(std::shared_ptr<GameObject>& obj)
{
    obj->SetInactive();
    auto oldHead = std::atomic_load(&m_Head);
    auto newNode = std::make_shared<Node>(obj, oldHead);

    while (!std::atomic_compare_exchange_weak(&m_Head, &newNode->Next, newNode)) {}
    m_CurrentPoolSize.fetch_add(1);
}

void ObjectPool::MaintainPoolBuffer()
{
    float percentage = static_cast<float>(m_CurrentPoolSize) / static_cast<float>(m_TotalObjectCount);
    if (percentage < m_PoolSizeLowerBoundPC || m_CurrentPoolSize < m_MinBufferSize)
    {
        FillPool(1);
    }
    else if (percentage > m_PoolSizeUpperBoundPC && m_CurrentPoolSize.load() > m_MinBufferSize)
    {
        RemoveHead();
    }
}
void ObjectPool::SetPoolSizeBoundPercentages(float lower, float upper)
{
    assert(lower > 0 && upper > lower);
    m_PoolSizeLowerBoundPC = lower;
    m_PoolSizeUpperBoundPC = upper;
}
void ObjectPool::RemoveHead()
{
    auto oldHead = std::atomic_load(&m_Head);
    do
    {
        if (!oldHead)
        {
            return;
        }
    }
    while (!std::atomic_compare_exchange_weak(&m_Head, &oldHead, oldHead->Next));

    m_CurrentPoolSize.fetch_sub(1);
    m_TotalObjectCount.fetch_sub(1);
}

ObjectPool* ObjectPoolManager::CreatePool(const std::string& poolName, const std::shared_ptr<GameObject>& prefab, int countIncreasePerExpansion, int initialAllocationCount, float lowerBoundPC, float upperBoundPC)
{
    m_Pools[poolName] = std::make_unique<ObjectPool>(prefab, countIncreasePerExpansion, initialAllocationCount, lowerBoundPC, upperBoundPC);
    m_PoolIterator = m_Pools.begin();
    return m_Pools[poolName].get();
}

std::shared_ptr<GameObject> ObjectPoolManager::GetPooledObject(const std::string& PoolName)
{
    return m_Pools[PoolName]->GetPooledObject();
}

void ObjectPoolManager::ReturnToPool(std::shared_ptr<GameObject> obj, const std::string& poolName)
{
    m_Pools[poolName]->AddToPool(obj);
}
void ObjectPoolManager::MaintainPoolBuffers(uintptr_t _unused)
{
    // note that it's fine for multiple threads to be maintaining the same pool and hence for this itr to loop around
    std::unique_lock<std::mutex> lock(m_MaintenanceMutex);
    auto itr = m_PoolIterator++;
    if (m_PoolIterator == m_Pools.end())
    {
        m_PoolIterator = m_Pools.begin();
    }
    itr->second->MaintainPoolBuffer();
}