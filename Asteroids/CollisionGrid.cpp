#include "CollisionGrid.h"
#include "GameObject.h"
#include "Components.h"
#include "JobSystem.h"
#include "Gamestate.h"

ObjectCollisionGrid::ObjectCollisionGrid()
{
    CompletedCollisionsThisFrame.set_lock_style(ReadersWriterLock::LockStyle::M2CV);
}

uint8_t ObjectCollisionGrid::InsertObject(GameObject* obj, int x, int y, uint16_t selfMask, uint16_t otherMask)
{
    int index = x + y * GRID_RESOLUTION;

    return MemoryPool.AllocateNode(index, obj, selfMask, otherMask);
}

void ObjectCollisionGrid::RemoveObject(uint8_t nodeIndex, int x, int y)
{
    int index = x + y * GRID_RESOLUTION;
    MemoryPool.DeallocateNode(nodeIndex, index);
}

void ObjectCollisionGrid::ResolveCollisionsOfCells(uintptr_t pData)
{
    JobData_Ints* data = reinterpret_cast<JobData_Ints*>(pData);
    int cellEndIndex = data->end;
    for (int cellIndex = data->start; cellIndex <= cellEndIndex; ++cellIndex)
    {
        if (MemoryPool.CellEmpty(cellIndex))
        {
            continue;
        }
        // use bit array to move through the block of memory and select valid nodes for checking
        uint32_t bitArray = MemoryPool.GetBitArray(cellIndex);
        uint32_t bitArray2 = bitArray;
        Node* node = MemoryPool.GetNode(cellIndex);
        size_t first = 0;
        size_t second = 1;
        while (bitArray > 0)
        {
            bitArray /= 2;
            bitArray2 = bitArray;
            second = first;
            while (bitArray2 > 0)
            {
                ++second;
                bitArray2 /= 2;
                auto comp = (node + first)->SelfCollisionMask & (node + second)->OtherCollisionMask;
                if (comp > 0)
                {
                    if (comp % 2 == 1)
                    {
                        // both tags had their last bit == 1, meaning they are taking up more than one cell in the grid, don't want to double count collisions
                        // check not in completed collisions
                        int id1 = (node + first)->Object->getId();
                        int id2 = (node + second)->Object->getId();

                        std::pair<int, int> idPair = (id1 < id2) ? std::make_pair(id1, id2) : std::make_pair(id2, id1);
                        if (CompletedCollisionsThisFrame.find(idPair) != CompletedCollisionsThisFrame.end())
                        {
                            (node + first)->Object->CollisionWith((node + second)->Object, (node + first)->SelfCollisionMask, (node + second)->SelfCollisionMask, CompletedCollisionsThisFrame, idPair);
                        }
                    }
                    else
                    {
                        (node + first)->Object->CollisionWithUnique((node + second)->Object, (node + first)->SelfCollisionMask, (node + second)->SelfCollisionMask);
                    }
                }
            }
        }
    }
}
void ObjectCollisionGrid::ClearFrameCollisionPairs(uintptr_t unused)
{
    CompletedCollisionsThisFrame.clear();
}


NodeMemoryPool::NodeMemoryPool() 
{
    size_t totalSize = GRID_RESOLUTION * GRID_RESOLUTION * NUMBER_OF_NODES * sizeof(Node);
    Memory = static_cast<Node*>(std::malloc(totalSize));

    for (int i = 0; i < GRID_RESOLUTION * GRID_RESOLUTION * NUMBER_OF_NODES; ++i) 
    {
        new (&Memory[i]) Node(nullptr,0, 0);
    }
}

NodeMemoryPool::~NodeMemoryPool() 
{
    for (int i = 0; i < GRID_RESOLUTION * GRID_RESOLUTION * NUMBER_OF_NODES; ++i)
    {
        Memory[i].~Node();
    }
    std::free(Memory);
}

uint8_t NodeMemoryPool::AllocateNode(int index, GameObject* obj, uint16_t selfMask, uint16_t otherMask)
{
    if (InUseBitArray[index] == 0xFFFFFFFF) return 255;
    Node* startNode = Memory + index * NUMBER_OF_NODES;

    if (Gamestate::instance->GetPhaseIndex() == 3)
    {
        startNode = 0;
    }

    uint32_t bitArray = InUseBitArray[index].load();
    for (int i = 0; i < 32; ++i)
    {
        uint32_t newArray = bitArray & (0x01 << i);
        if (newArray == 0)
        {
            newArray = bitArray | (0x01 << i);
            if (InUseBitArray[index].compare_exchange_strong(bitArray, newArray))
            {
                Node* node = startNode + i;
                new (node) Node(obj, selfMask, otherMask);
                return i;
            }
        }
    }
    return 255;
}

void NodeMemoryPool::DeallocateNode(uint8_t nodeIndex, int index) 
{
    Node* node = Memory + index * NUMBER_OF_NODES + nodeIndex;
    if (node->Object == nullptr)
    {
        index = 0;
    }
    node->Object = nullptr;
    node->SelfCollisionMask = 0;
    node->OtherCollisionMask = 0;
    InUseBitArray[index].fetch_and(~(0x01 << nodeIndex));
}

