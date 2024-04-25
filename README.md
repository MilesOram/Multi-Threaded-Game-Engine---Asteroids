# Asteroids
WIP. README is unfinished.
Uses SFML library for graphics, 32-bit, C++ 17.

Recreation of Asteroids, with addition that every 30 seconds there is increased asteroid spawn rate and increased fire rate for a period.

General simplified structure:

![image](https://github.com/MilesOram/Asteroids/assets/86774698/37dd3353-e852-4466-aaec-41187d7c8d94)


Previously built a single-threaded version of this. Current version is multi-threaded and includes:

# Lock-free object pool
For GameObjects.
Allows dynamic resizing at runtime depending on config settings which can also be changed in anticipation of higher/lower demand.
Originally it was templated so that it could store: T prefab and then make_shared<T>(prefab) to make copies and static_assert that it derived from GameObject.
Instead implemented a clone function

# Job System
Game loop is divided into the following phases - Update->Collision->Cleanup->Snapshot (rendering occurs during update and collision phases). 
Phase transition is managed by a job, this job creates all the jobs for the next phase and stores them in a buffer queue, it then waits for other threads to finish and sync with the main thread if needed. 
It then swaps the buffer and signals all the threads. 
This new batch of jobs contains the next phase transition job on the end.

# Component system
Similar to Unity, currently includes collision components (box,circle and polygon) and a pooled object component for objects that belong to a pool.

I didn't like using type indentification to store the components in a map, so I wanted the keys to be ints, specifi

# Collision
Uses GJK for all intersection testing (excluding circle-circle) and double dispatch to select the relevant function.
Screen is partitioned into a grid, each object uses a phase box to place it in the cells in the grid that it occupies.
Could use a spatial hash grid but wanted something that allowed concurrent lock-free insertion and removal, at the cost of a larger memory footprint.
The grid allocates a large block of memory which it divides into cells which contain nodes for each object.
The nodes also contain the collision tags of that object and the tags it is looking for, the result is a check for (currentNode->selfTags & nextNode->otherTags > 0).
This tag comparison and grid design in general in focused on good cache locality.
The allocator tracks in-use nodes with a bit array, currently these are stored in a separate array but I intend to move them to the start of that cell's memory.
So the memory will look like:

BitArray | Node | Node | Node...

BitArray | Node | Node | Node...
etc.

Working in 32-bit, the node struct contains a GameObject*, and two uint16_t for both tags - 8 bytes total.
During collision resolution, each thread executes a job which acts on a contiguous chunk of the grid memory and tests the pairs within each 'cell'.
The fixed size grid works due to the constrained nature of the game and typically asteroids are well-distributed throughout the screen.
I need to implement an overflow solution for when there are too many objects per cell, this would look something like an extra chunk of memory at the end of the grid.
The allocator can assign this to a given cell and then reclaim it later, if this is frequent then the design starts to fall apart.
However I also intend to make an upkeep job to defrag nodes in each cell.
As play progresses, each 'cell' will be packed less tightly.
Currently the effect of this is negligible since during each update call, objects fully remove themselves from all cells in the collision grid.
Then they re-insert themselves.
Obviously I need to change this so that it finds the difference between the old and new phase box and ignores the cells that overlap.
The reason I've yet to do this is because objects store the node offset (an offset such that startNode + offset will be the node containing this object) but the offsets are stored in a vector.
This vector is ordered in the way the phase box is iterated through, so it becomes messy when only partially updating.
I intend to find a simpler solution that is cleaner, and also makes it easier to complete concurrent defrags if that's possible.

# Rendering
Main thread does all the rendering.
I believe SFML allows any thread to do the rendering (not just the main) so long as no other thread is also trying to use the window.
In theory this project could have no main thread if I slightly tweaked the counters for the phase transitions.
But it makes sense to have one thread assigned to only rendering.
Game objects are stored as shared_ptrs in a multiset, ordered by their texture and then id.
This groups objects of the same texture for faster draw calls.
I also wanted to try reversing the direction of iterating through the set each frame to see if this would make any difference, since the current texture would be the same at the end of a frame and the start of the next.
Object insertion/removal/cleanup is queued during either the update or collision step, and carried out during the cleanup phase.
The double buffering is achieved by storing the GameObject's rotation and position into that object's sprite at the end of each frame.
The main thread then renders using the sprite's pos and rot, which is untouched during update/collision phases.


