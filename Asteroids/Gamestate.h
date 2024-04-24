#pragma once
#include "Top.h"
#include "JobSystem.h"

class ObjectPool;
class ObjectPoolManager;
class ObjectCollisionGrid;
class Asteroid;
class Projectile;
class PlayerShip;
class GameObject;

struct TextureAndIDComparator
{
	bool operator()(const std::shared_ptr<GameObject>& lhs, const std::shared_ptr<GameObject>& rhs) const;
};
// the multiset orders gameobjects by texture and then id, such that objects are grouped by texture for more efficient rendering
typedef std::multiset<std::shared_ptr<GameObject>, TextureAndIDComparator> GameObjectMultiset;

// job data, will be stored and passed to a job through casting JobData_Iterators* to uintptr, stores iterators for dividing game objects
// into chunks, to split work equally among threads
struct JobData_Iterators
{
	GameObjectMultiset::iterator start;
	GameObjectMultiset::iterator end;

	JobData_Iterators(GameObjectMultiset::iterator _start, GameObjectMultiset::iterator _end) : start(_start), end(_end) {}
};
// job data, will be stored and passed to a job through casting JobData_Ints* to uintptr, stores ints for dividing e.g. collision grid
// into chunks, to split work equally among threads
struct JobData_Ints
{
	int start;
	int end;

	JobData_Ints(int a = 0, int b = 0) : start(a), end(b) {}
};

// similar to above, but no need for storage of data and casting T* to uintptr, instead uses a union to directly store the data,
// so use 'value' for creating job data, and then 'indices' for extraction
struct JobData_Indices
{
	union
	{
		std::pair<uint16_t, uint16_t> alignas(4) indices;
		uintptr_t value;
	};

	JobData_Indices(uint16_t a = 0, uint16_t b = 0) : indices(a,b) {}
	JobData_Indices(uintptr_t x) : value(x) {}
};

// used for repeated large batches of jobs for major functions where you want the data to stay in scope between frames (e.g. update calls, collision calls)
// assumes work should be divided equally among all threads, hence length of NUM_THREADS
struct JobPrepataionData
{
	JobSystem::Counter* Counter;
	std::vector<JobSystem::Declaration> Declarations;
	JobPrepataionData(size_t size = NUM_THREADS, JobSystem::Counter* counter = nullptr) : Declarations(size), Counter(counter) {}
};

// used to transition between phases e.g. update->collision, stores all the jobs for the next phase and a flag for whether to sync
// with the main thread at the end of the next phase
struct ThreadPhaseTransitionData
{
	std::vector<JobSystem::Declaration>* NextDeclarations;
	bool IncludeMainThread;
	ThreadPhaseTransitionData(std::vector<JobSystem::Declaration>* decls, bool mainT = false) : NextDeclarations(decls), IncludeMainThread(mainT) {}
};

// used to better facilitate phase transitions, the thread managing the transition needs to also set up the next transition
// so giving it T** pA, it can copy T* A, then make pA point to the next transition's data - no need to remake jobs
struct pTPTD
{
	ThreadPhaseTransitionData* pData;
};

// each thread has a unique index, used for certain wait-free functionality
extern thread_local int ThreadIndex;

// singleton class to manage the overall running of the game, handles all rendering, owns various managers, stores all active objects
// facilitates phase transitions for jobs
class Gamestate
{
	
public:
	static Gamestate* instance;
	Gamestate();
	// game flow
	void BeginPlay();
	void Draw(sf::RenderWindow& _window);
	void CleanUp();
	// inits
	void InitialiseTextures();
	void InitialiseObjectPools();
	void InitialiseScreenText();
	void InitialisePlayer();
	// update for the gamestate
	void Update(uintptr_t);
	void SpawnLargeAsteroidOffscreen();
	// score
    void AddScore(int score) { m_TotalScore += score; }
    int GetScore() const { return m_TotalScore; }
	// collision grid
	uint8_t AddToCollisionGrid(GameObject* obj, int newX, int newY, uint16_t selfMask, uint16_t otherMask);
	void RemoveFromCollisionGrid(uint8_t nodeIndex, int prevX, int prevY);
	// GameObject management
	void AddToActiveObjects(const std::shared_ptr<GameObject>&);
	void AddToCleanupObjects(std::shared_ptr<GameObject> obj);
	void AddToCleanupObjectsDelayed(std::shared_ptr<GameObject> obj);
	std::shared_ptr<GameObject> GetPooledObject(const std::string& PoolName);
	// indices
	int ObtainUniqueThreadLocalIndex() { return m_ThreadIndexCounter.fetch_add(1); }
	int GetPhaseIndex() const { return m_PhaseIndex % m_JobPhaseTransitions.size(); }

private:
	// score and timers
	int m_TotalScore = 0;
	float m_AsteroidTimer = 3.0f;
	float m_AsteroidCD = 3.0f;
	float m_OverdriveTimer = 0.0f;
	const float m_OverdriveCD = 30.0f;
	bool m_Overdrive = false;
	// dt for the frame
	float m_DeltaTime = 0.f;
	// gives unique index to each thread
	std::atomic<int> m_ThreadIndexCounter;
	// screen text and textures
	sf::Font ScreenFont;
	sf::Text m_OverdriveText;
	std::vector<sf::Text> ScreenText;
	std::vector<sf::Texture> ObjectTextures;
	// manages object pools
	std::shared_ptr<ObjectPoolManager> m_PoolManager;
	// divides space into cells for more efficient collision checks
	std::shared_ptr<ObjectCollisionGrid> m_CollisionGrid;
	// GameObject management
	std::shared_ptr<PlayerShip> m_Player;
	GameObjectMultiset m_AllActiveGameObjects;
	std::vector<std::vector<std::shared_ptr<GameObject>>> m_ObjectsToAdd;
	std::vector<std::vector<std::shared_ptr<GameObject>>> m_ObjectsToCleanUp;
	// phase index - denotes phase e.g. update, collision, cleanup, snapshot
	int m_PhaseIndex = 0;
	// index is incremented after each phase, and loops round, used to get job data from m_JobPhaseTransitions and m_JobPrepData
	int IncrementAndGetPhaseIndex() { return ++m_PhaseIndex % m_JobPhaseTransitions.size(); }
	// jobs, counters and job data 
	JobSystem::Counter* m_PhaseCounter; 
	JobSystem::Counter* m_UnusedTransitionCounter;
	JobSystem::Declaration m_PhaseTransitionDecl;
	ThreadPhaseTransitionData* m_CurrentTransitionData;

	std::vector<JobData_Ints> m_JobInts;
	std::vector<JobData_Iterators> m_JobIterators;
	std::vector<std::unique_ptr<JobPrepataionData>> m_JobPrepData;
	std::vector<std::unique_ptr<ThreadPhaseTransitionData>> m_JobPhaseTransitions;
	// job setup
	void MakeUpdateJobData();
	void MakeCollisionJobData();
	void CreateUpdateJobs();
	void CreateCollisionJobs();
	void CreateCleanupJobs();
	void CreateSnapshotJobs();
	void CreateUpkeepJobs();
	void CreatePhaseTransitionDeclaration();
	// job functions
	void ManageThreadPhaseTransition(uintptr_t data);
	void UpdateGameObjectSection(uintptr_t data);
	void CreateSnapshotForGameObjectSection(uintptr_t data);
	void ProcessInactiveObjects(uintptr_t data);
};
