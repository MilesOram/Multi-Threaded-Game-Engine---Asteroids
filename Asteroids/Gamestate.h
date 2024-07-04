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
class ParticleSystem;

struct TextureAndIDComparator
{
	bool operator()(const std::shared_ptr<GameObject>& lhs, const std::shared_ptr<GameObject>& rhs) const;
};
// the multiset orders GameObjecs by texture and then id, such that objects are grouped by texture for more efficient rendering
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

// used to signal what would otherwise be a job for the main thread to change the colour of the glow, but the main thread
// doesn't access the job queue and I don't want other threads interacting with the shaders
enum class GlowColourChange { RED, BLUE, NOCHANGE};
enum class GlowRadiusChange { FULL, HALF, NOCHANGE};

class Gamestate {
public:
	static Gamestate* instance;

	Gamestate();
	~Gamestate();

	// Game flow
	void BeginPlay();

	// Score management
	void AddScore(int score) { m_TotalScore += score; }
	int GetScore() const { return m_TotalScore; }

	// Collision grid management
	uint8_t AddToCollisionGrid(GameObject* obj, int newX, int newY, uint16_t selfMask, uint16_t otherMask);
	void RemoveFromCollisionGrid(uint8_t nodeIndex, int prevX, int prevY);

	// GameObject management
	void AddToActiveObjects(const std::shared_ptr<GameObject>& obj);
	void AddToCleanupObjects(std::shared_ptr<GameObject> obj);
	void AddToCleanupObjectsDelayed(std::shared_ptr<GameObject> obj);
	std::shared_ptr<GameObject> GetPooledObject(const std::string& PoolName);

	// Indices
	int ObtainUniqueThreadLocalIndex() { return m_ThreadIndexCounter.fetch_add(1); }
	int GetPhaseIndex() const { return m_PhaseIndex % m_JobPhaseTransitions.size(); }

	// Signals
	void SignalLightUp();
	void SignalLightDown();

#if USE_CPU_FOR_OCCLUDERS
	int* GetPixelPrepPtr(int index);
#endif

private:
	// Score and timers
	sf::Clock m_GameClock;
	int m_TotalScore = 0;
	float m_AsteroidTimer = 1.5f;
	float m_AsteroidCD = 1.5f;
	float m_OverdriveTimer = 0.0f;
	const float m_OverdriveCD = 30.0f;
	sf::Time m_Elapsed;

	// Tuning variables
	const float m_BaseAsteroidCD = 1.5f;
	const float m_OverdriveAsteroidCD = 0.5f;
	const float m_BaseProjectileCD = 0.25f;
	const float m_OverdriveProjectileCD = 0.1f;
	const sf::Glsl::Vec4 m_BaseGlowColor = sf::Glsl::Vec4(0.3f, 1.0f, 1.0f, 1.0f);
	const sf::Glsl::Vec4 m_OverdriveGlowColor = sf::Glsl::Vec4(1.0f, 0.3f, 0.3f, 1.0f);
	const float m_PulseSpeed = 500;

	// Tracking
	float m_PulsePosition = -1.f;
	sf::Vector2f m_LastShipPos = { 0,0 };
	bool m_SubstantialShipMovement = true;
	bool m_Overdrive = false;

	// Glow
	GlowColourChange m_GlowColourChange = GlowColourChange::NOCHANGE;
	GlowRadiusChange m_GlowRadiusChange = GlowRadiusChange::NOCHANGE;
	const float m_LowGlowRadius = 500.f;
	const float m_HighGlowRadius = 1000.f;
	const float m_PulseWidth = 280.f;

	// Fog
	const float m_NearFogRadius = 400.f;
	const float m_FarFogRadius = 800.f;

	// Delta time for the frame
	float m_DeltaTime = 0.f;

	// Thread index counter
	std::atomic<int> m_ThreadIndexCounter;

	// Screen text and textures
	sf::Font ScreenFont;
	sf::Text m_OverdriveText;
	std::vector<sf::Text> ScreenText;
	std::vector<sf::Texture> ObjectTextures;

	// Managers
	std::shared_ptr<ObjectPoolManager> m_PoolManager;
	std::shared_ptr<ObjectCollisionGrid> m_CollisionGrid;

	// GameObject management
	std::shared_ptr<PlayerShip> m_Player;
	std::shared_ptr<ParticleSystem> m_ParticleSystem;
	GameObjectMultiset m_AllActiveGameObjects;
	std::vector<std::vector<std::shared_ptr<GameObject>>> m_ObjectsToAdd;
	std::vector<std::vector<std::shared_ptr<GameObject>>> m_ObjectsToCleanUp;

	// Job system
	JobSystem::Counter* m_PhaseCounter;
	JobSystem::Counter* m_UnusedTransitionCounter;
	JobSystem::Declaration m_PhaseTransitionDecl;
	ThreadPhaseTransitionData* m_CurrentTransitionData;
	std::vector<JobData_Ints> m_JobInts;
	std::vector<JobData_Iterators> m_JobIterators;
	std::vector<std::unique_ptr<JobPrepataionData>> m_JobPrepData;
	std::vector<std::unique_ptr<ThreadPhaseTransitionData>> m_JobPhaseTransitions;

	// Phase index
	int m_PhaseIndex = 0;
	int IncrementAndGetPhaseIndex() { return ++m_PhaseIndex % m_JobPhaseTransitions.size(); }

	// Job setup
	void MakeUpdateJobData();
	void MakeCollisionJobData();
	void CreateUpdateJobs();
	void CreateCollisionJobs();
	void CreateCleanupJobs();
	void CreateSnapshotJobs();
	void CreateUpkeepJobs();
	void CreatePhaseTransitionDeclaration();

	// Job functions
	void ManageThreadPhaseTransition(uintptr_t data);
	void UpdateGameObjectSection(uintptr_t data);
	void CreateSnapshotForGameObjectSection(uintptr_t data);
	void ProcessInactiveObjects(uintptr_t data);
	void UpdateParticleSystem(uintptr_t data);

	// Shaders, vertex array and textures
	sf::Shader m_LightenShader;
	sf::Shader m_GlowShader;
	sf::Shader m_BlurShader;
	sf::Shader m_FogShader;
	sf::VertexArray m_FullScreenQuad;
	sf::RenderTexture m_BufferRenderTexture1;
	sf::RenderTexture m_BufferRenderTexture2;

#if USE_CPU_FOR_OCCLUDERS
	// Occluder map
	sf::Texture m_MainTexture;
	// Pixels used to update m_MainTexture
	int* m_PixelPrep;
#else 
	void DrawAsteroidVertexArray(sf::VertexArray& vertices, sf::RenderStates& states, std::vector<std::pair<std::shared_ptr<GameObject>, float>>& objs, sf::RenderTexture& tex, sf::Shader& shader);
	
	// Helper functions for asteroid vertex array
	static void SortObjectsByDistance(std::vector<std::pair<std::shared_ptr<GameObject>, float>>& objs);
	static float CalculateDistanceSquared(const sf::Vector2f& point1, const sf::Vector2f& point2);
	static void RotateBox(float* coords, float sinAngle, float cosAngle);
#endif

	// Initialization functions
	void InitialiseTextures();
	void InitialiseObjectPools();
	void InitialiseScreenText();
	void InitialisePlayer();
	void InitialiseShaders();

	// Game flow functions
	void SyncWithOtherThreads();
	inline void RestartClock();
	inline void ClearCleanUpObjects();
	inline void CheckGlowShaderUniforms();

	// Main loop functions
	void Draw(sf::RenderWindow& window);
	void Update(uintptr_t);
	void CleanUp();
	void SpawnLargeAsteroidOffscreen();
	void CheckShipPosition();
};
