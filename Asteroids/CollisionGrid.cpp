#include "CollisionGrid.h"
#include "GameObject.h"
#include "Components.h"
#include "JobSystem.h"
#include "Gamestate.h"
#include <immintrin.h>

ObjectCollisionGrid::ObjectCollisionGrid()
{
    m_CompletedCollisionsThisFrame.set_lock_style(ReadersWriterLock::LockStyle::M2CV);
#if USE_CPU_FOR_OCCLUDERS
    for (int i = 0; i < PATCH_SIZE * NUM_THREADS * 9; ++i)
    {
        m_PixelBufferThread[i] = 0;
    }
#endif
}

uint8_t ObjectCollisionGrid::InsertObject(GameObject* obj, int x, int y, uint16_t selfMask, uint16_t otherMask)
{
    int index = x + y * GRID_RESOLUTION;

    return m_MemoryPool.AllocateNode(index, obj, selfMask, otherMask);
}

void ObjectCollisionGrid::RemoveObject(uint8_t nodeIndex, int x, int y)
{
    int index = x + y * GRID_RESOLUTION;
    m_MemoryPool.DeallocateNode(nodeIndex, index);
}

void ObjectCollisionGrid::ResolveCollisionsOfCells(uintptr_t pData)
{
    JobData_Ints* data = reinterpret_cast<JobData_Ints*>(pData);
    int cellEndIndex = data->end;

    for (int cellIndex = data->start; cellIndex <= cellEndIndex; ++cellIndex)
    {
#if USE_CPU_FOR_OCCLUDERS
        int x = ((cellIndex % GRID_RESOLUTION) * SCREEN_WIDTH) / GRID_RESOLUTION;
        int y = (cellIndex / GRID_RESOLUTION * SCREEN_HEIGHT) / GRID_RESOLUTION;
        int pos = x + y * SCREEN_WIDTH;
        int* ptr = Gamestate::instance->GetPixelPrepPtr(pos);
        for (int i = 0; i < PATCH_SIZE; ++i)
        {
            std::memset(ptr + i * SCREEN_WIDTH, 0, PATCH_SIZE*sizeof(int));
        }
        if (m_MemoryPool.CellEmpty(cellIndex))
        {
            continue;
        }
#endif
        // use bit array to move through the block of memory and select valid nodes for checking, each 1 denotes a valid node to check
        uint32_t bitArray = m_MemoryPool.GetBitArray(cellIndex);
        uint32_t bitArray2 = bitArray;
        Node* node = m_MemoryPool.GetNode(cellIndex);

        // node offsets for iterating
        size_t first = 0;
        size_t second = 1;
        bool filled = false;

        // for as long as there are valid nodes check check i.e. bits with value 1 in the bit array
        while (bitArray > 0)
        {

#if USE_CPU_FOR_OCCLUDERS
            // only need the following if preparing texture for occluders of glow outside of shaders
            filled = false;
            if (((node + first)->SelfCollisionMask & 0b10) > 0 && (node + first)->Object->GetOccluder() && !filled)
            {
                // x,y grid coords
                int x = cellIndex % GRID_RESOLUTION;
                int y = cellIndex / GRID_RESOLUTION;

                // each thread owns part of m_PixelBufferThread which it uses for preparing the pixel coords for point in polygon checks
                float* xStart = m_PixelBufferThread + ThreadIndex * PATCH_SIZE * 9;
                float* yStart = xStart + PATCH_SIZE;

                float xPos = static_cast<float>(x * SCREEN_WIDTH) / GRID_RESOLUTION;
                float yPos = static_cast<float>(y * SCREEN_HEIGHT) / GRID_RESOLUTION;

                // prepare x coords for SIMD of pixels in current patch
                for (int i = 0; i < PATCH_SIZE; i +=8)
                {
                    *(xStart + i) = xPos + i;
                    *(xStart + i + 1) = xPos + i + 1;
                    *(xStart + i + 2) = xPos + i + 2;
                    *(xStart + i + 3) = xPos + i + 3;
                    *(xStart + i + 4) = xPos + i + 4;
                    *(xStart + i + 5) = xPos + i + 5;
                    *(xStart + i + 6) = xPos + i + 6;
                    *(xStart + i + 7) = xPos + i + 7;
                }
                // prepare duplicate y coords for SIMD of pixels in current patch
                int num = 0;
                for (int i = 0; i < PATCH_SIZE*8; i += 8)
                {
                    *(yStart + i) = yPos + num;
                    *(yStart + i + 1) = yPos + num;
                    *(yStart + i + 2) = yPos + num;
                    *(yStart + i + 3) = yPos + num;
                    *(yStart + i + 4) = yPos + num;
                    *(yStart + i + 5) = yPos + num;
                    *(yStart + i + 6) = yPos + num;
                    *(yStart + i + 7) = yPos + num;
                    ++num;
                }
                // process patch and return whether the entire patch is filled or not
                filled = (node + first)->Object->GetComponent<CollisionComponent>()->CheckPointsInCollider(Gamestate::instance->GetPixelPrepPtr(static_cast<int>(xPos) + static_cast<int>(yPos) * SCREEN_WIDTH), xStart, yStart);
            }
            else if ((node + first)->SelfCollisionMask > 0 && (node + first)->Object->GetOccluder() && !filled)
            {
                int x = ((cellIndex % GRID_RESOLUTION) * SCREEN_WIDTH) / GRID_RESOLUTION;
                int y = (cellIndex / GRID_RESOLUTION * SCREEN_HEIGHT) / GRID_RESOLUTION;

                int pos = x + y * SCREEN_WIDTH;
                int* ptr = Gamestate::instance->GetPixelPrepPtr(pos);

                // loop unrolling takes about 40% of the time of standard iteration
                for (int i = 0; i < PATCH_SIZE; ++i)
                {
                    for (int j = 0; j < PATCH_SIZE; j+=8)
                    {
                        ptr[i * SCREEN_WIDTH + j] = 0xFFFFFFFF;
                        ptr[i * SCREEN_WIDTH + j+1] = 0xFFFFFFFF;
                        ptr[i * SCREEN_WIDTH + j+2] = 0xFFFFFFFF;
                        ptr[i * SCREEN_WIDTH + j+3] = 0xFFFFFFFF;
                        ptr[i * SCREEN_WIDTH + j+4] = 0xFFFFFFFF;
                        ptr[i * SCREEN_WIDTH + j+5] = 0xFFFFFFFF;
                        ptr[i * SCREEN_WIDTH + j+6] = 0xFFFFFFFF;
                        ptr[i * SCREEN_WIDTH + j+7] = 0xFFFFFFFF;
                    }
                }
                filled = true;
            }
#endif

            bitArray /= 2;
            bitArray2 = bitArray;
            second = first;

            // with constant first node, test all following pairs with the later nodes
            while (bitArray2 > 0)
            {
                ++second;
                bitArray2 /= 2;
                auto comp = (node + first)->SelfCollisionMask & (node + second)->OtherCollisionMask;
                if (comp > 0)
                {
                    if (comp % 2 == 1 && comp > 1)
                    {
                        // both tags had their last bit == 1, meaning they are taking up more than one cell in the grid, don't want to double count collisions
                        // check not in completed collisions
                        int id1 = (node + first)->Object->getId();
                        int id2 = (node + second)->Object->getId();

                        std::pair<int, int> idPair = (id1 < id2) ? std::make_pair(id1, id2) : std::make_pair(id2, id1);
                        if (m_CompletedCollisionsThisFrame.find(idPair) != m_CompletedCollisionsThisFrame.end())
                        {
                            // send to intersection testing
                            (node + first)->Object->CollisionWith((node + second)->Object, (node + first)->SelfCollisionMask, (node + second)->SelfCollisionMask, m_CompletedCollisionsThisFrame, idPair);
                        }
                    }
                    else
                    {
                        // send to intersection testing
                        (node + first)->Object->CollisionWithUnique((node + second)->Object, (node + first)->SelfCollisionMask, (node + second)->SelfCollisionMask);
                    }
                }
            }
            ++first;
            second = first + 1;
        }
    }
}
void ObjectCollisionGrid::ClearFrameCollisionPairs(uintptr_t unused)
{
    m_CompletedCollisionsThisFrame.clear();
}

// this memory is allocated before the game starts, and is freed after the game ends - malloc is not used anywhere during the game
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

// determines the next available memory for a node in the specified cell, constructs the new node with placement new
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

// deallocating a node means setting it to default values and amending the bitArray atomically
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

