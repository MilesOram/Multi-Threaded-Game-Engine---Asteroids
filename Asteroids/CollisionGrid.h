#pragma once
#include "Top.h"
#include "ThreadSafeSet.h"
#include <unordered_map>
#include <mutex>
#include <new>
#include <stdlib.h>

class GameObject;

struct alignas(8) Node
{
    GameObject* Object;
    // Ideally want to eval whether it's a valid collision pair without entering the game objects, so store this info outside the class and in each node
    // see GamestateTemplates.h for tag info, the final bit is used to denote whether the Object exists in multiple cells, if not it's more simple
    // if so, store collisions that have been resolved this frame and check each time the current pair hasn't already been resolved in a different cell
    uint16_t SelfCollisionMask=0;
    uint16_t OtherCollisionMask=0;
    Node(GameObject* _Object, uint16_t _SelfCollisionMask, uint16_t _OtherCollisionMask) : Object(_Object), SelfCollisionMask(_SelfCollisionMask), OtherCollisionMask(_OtherCollisionMask) {}
    Node() : Object(nullptr) {}
};
// allocates memory for nodes used in CollisionGrid, fixed block of size: GRID_RESOLUTION * GRID_RESOLUTION * NUMBER_OF_NODES * sizeof(Node)
// space for up to NUMBER_OF_NODES objects per cell in the grid, check each pair's tags and then call check collision function
// pros: fast concurrent insertion and removal of objects during the update phase, good cache locality
// cons: worse memory footprint and potentially slow pair checks in each cell, depending on fragmentation, (requires fallback expansion in case of too many objects)
// can potentially be offset with an upkeep job to defrag and shift all the nodes down, have yet to figure out a good way to do this
// since each collision component stores a list of offsets, these would need to be updated if nodes were moved, makes it slightly awkward to parallelise
class NodeMemoryPool
{
private:
    static const int NUMBER_OF_NODES = 16;
    Node* Memory;
    // track memory usage at each grid location, eaach bit represents in use/not in use
    std::atomic<uint32_t> InUseBitArray[GRID_RESOLUTION * GRID_RESOLUTION];
public:
    NodeMemoryPool();
    ~NodeMemoryPool();
    uint8_t AllocateNode(int index, GameObject* obj, uint16_t selfMask, uint16_t otherMask);
    void DeallocateNode(uint8_t nodeIndex, int index);
    bool CellEmpty(int index) { return InUseBitArray[index] == 0; }
    uint32_t GetBitArray(int index) const { return InUseBitArray[index]; }
    Node* GetNode(int index) { return Memory + index * NUMBER_OF_NODES; }
};

class ObjectCollisionGrid
{
private:
    NodeMemoryPool MemoryPool;
    // set allows concurrent reads (happens frequently) but write locks the whole set (happens rarely)
    thread_safe_set<std::pair<int,int>> CompletedCollisionsThisFrame;
public:
    ObjectCollisionGrid();
    // insertion/removal
    uint8_t InsertObject(GameObject* obj, int x, int y, uint16_t selfMask, uint16_t otherMask);
    void RemoveObject(uint8_t nodeIndex, int x, int y);
    // jobs
    // checks for collisions in a range specified by pData
    void ResolveCollisionsOfCells(uintptr_t pData);
    // clears CompletedCollisionsThisFrame during cleanup phase
    void ClearFrameCollisionPairs(uintptr_t unused);
};

