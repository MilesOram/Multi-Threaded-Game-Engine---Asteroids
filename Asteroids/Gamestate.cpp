#include "Gamestate.h"
#include "ObjectPool.h"
#include "Components.h"
#include "PlayerShip.h"
#include "JobSystem.h"
#include "GameObject.h"
#include "Asteroid.h"
#include "Projectile.h"
#include "CollisionGrid.h"

thread_local int ThreadIndex;

bool TextureAndIDComparator::operator()(const std::shared_ptr<GameObject>& lhs, const std::shared_ptr<GameObject>& rhs) const
{
	if (lhs->GetSprite().getTexture() != rhs->GetSprite().getTexture())
	{
		return lhs->GetSprite().getTexture() < rhs->GetSprite().getTexture();
	}
	return lhs->getId() < rhs->getId();
}

Gamestate::Gamestate()
{
	if (instance == nullptr) instance = this;
	m_PoolManager = std::make_shared<ObjectPoolManager>();
	m_CollisionGrid = std::make_shared<ObjectCollisionGrid>();
	m_ObjectsToAdd = std::vector<std::vector<std::shared_ptr<GameObject>>>(NUM_THREADS, std::vector<std::shared_ptr<GameObject>>());
	m_ObjectsToCleanUp = std::vector<std::vector<std::shared_ptr<GameObject>>>(NUM_THREADS, std::vector<std::shared_ptr<GameObject>>());
}

void Gamestate::Update(uintptr_t unused)
{
	m_AsteroidTimer += m_DeltaTime;
	m_OverdriveTimer += m_DeltaTime;

	if (m_AsteroidTimer > m_AsteroidCD)
	{
		SpawnLargeAsteroidOffscreen();
		m_AsteroidTimer = 0;
	}
	if (m_OverdriveTimer > m_OverdriveCD)
	{
		m_AsteroidCD = 0.8f;
		m_Player->SetProjectileCD(0.1f);
		m_OverdriveTimer = 0;
		m_Overdrive = true;
	}
	else if (m_OverdriveTimer > 5 && m_Overdrive)
	{
		m_AsteroidCD = 3;
		m_Player->SetProjectileCD(0.25f);
		m_OverdriveTimer = 0;
		m_Overdrive = false;
	}
}

Gamestate* Gamestate::instance{ nullptr };

int random_int(int range_lower, int range_upper)
{
	if (range_lower > range_upper) throw std::out_of_range("rand int bad");
	if (range_upper == range_lower) return range_lower;
	std::random_device rd;
	std::mt19937 rng(rd());
	std::uniform_int_distribution<std::mt19937::result_type> rnd_dist(range_lower, range_upper);
	return rnd_dist(rng);
}

void Gamestate::InitialiseScreenText()
{
	ScreenFont.loadFromFile("Assets/Roboto-Regular.ttf");
	ScreenText.emplace_back();
	// score text
	ScreenText[0].setFont(ScreenFont);
	ScreenText[0].setCharacterSize(12);
	ScreenText[0].setFillColor(sf::Color::White);
	ScreenText[0].setStyle(sf::Text::Bold | sf::Text::Underlined);
	ScreenText[0].setPosition(30, 5);
	ScreenText[0].setString("Score: ");
	for (int i = 0; i < 3; ++i)
	{
		ScreenText.push_back(ScreenText[0]);
	}
	// score value
	ScreenText[1].setPosition(80, 5);
	// lives text
	ScreenText[2].setPosition(30, 25);
	ScreenText[2].setString("Lives: ");
	// lives value
	ScreenText[3].setPosition(80, 25);

	// m_Overdrive text
	m_OverdriveText.setFont(ScreenFont);
	m_OverdriveText.setCharacterSize(24);
	m_OverdriveText.setFillColor(sf::Color::Red);
	m_OverdriveText.setStyle(sf::Text::Bold);
	m_OverdriveText.setPosition(80, 120);
	m_OverdriveText.setString("WARNING - HIGH ASTEROID DENSITY");

}
void Gamestate::InitialiseTextures()
{
	ObjectTextures = std::vector<sf::Texture>(5);
	ObjectTextures[0].loadFromFile("Assets/Ship.png");
	ObjectTextures[1].loadFromFile("Assets/Projectile.png");
	ObjectTextures[2].loadFromFile("Assets/AsteroidL.png");
	ObjectTextures[3].loadFromFile("Assets/AsteroidM.png");
	ObjectTextures[4].loadFromFile("Assets/AsteroidS.png");
}


void Gamestate::Draw(sf::RenderWindow& window)
{
	// Draw all Game Objects
	for (auto& obj : m_AllActiveGameObjects)
	{
		window.draw(obj->GetSprite());
	}

	// Set values for screen text and draw
	ScreenText[1].setString(std::to_string(m_TotalScore));
	ScreenText[3].setString(std::to_string(m_Player->GetLives()));
	for (size_t i = 0; i < ScreenText.size(); ++i)
	{
		window.draw(ScreenText[i]);
	}

	if (m_Overdrive)
	{
		window.draw(m_OverdriveText);
	}
}
void Gamestate::SpawnLargeAsteroidOffscreen()
{
	std::shared_ptr<GameObject> ast = GetPooledObject(Asteroid::AsteroidLargePoolName);
	sf::Vector2f newPos;
	switch (random_int(0, 3))
	{
	case 0:
		newPos = { static_cast<float>(random_int(0, SCREEN_WIDTH)), -5.f };
		break;
	case 1:
		newPos = { static_cast<float>(random_int(0, SCREEN_WIDTH)), SCREEN_HEIGHT + 5.f };
		break;
	case 2:
		newPos = { -5.f, static_cast<float>(random_int(0, SCREEN_HEIGHT)) };
		break;
	case 3:
		newPos = { SCREEN_WIDTH + 5.f, static_cast<float>(random_int(0, SCREEN_HEIGHT)) };
		break;
	}
	ast->ReinitialiseObject(newPos, 0, ast);
}

void Gamestate::AddToActiveObjects(const std::shared_ptr<GameObject>& obj)
{
	m_ObjectsToAdd[ThreadIndex].push_back(obj);
}

void Gamestate::RemoveFromCollisionGrid(uint8_t nodeIndex, int prevX, int prevY)
{
	m_CollisionGrid->RemoveObject(nodeIndex, prevX, prevY);
}
uint8_t Gamestate::AddToCollisionGrid(GameObject* obj, int newX, int newY, uint16_t selfMask, uint16_t otherMask)
{
	return m_CollisionGrid->InsertObject(obj, newX, newY, selfMask, otherMask);
}
void Gamestate::AddToCleanupObjects(std::shared_ptr<GameObject> obj)
{
	m_ObjectsToCleanUp[ThreadIndex].push_back(obj);
	auto x = JobData_Indices(ThreadIndex, static_cast<uint16_t>(m_ObjectsToCleanUp[ThreadIndex].size() - 1));
	JobSystem::AddJobToBuffer({
	{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::ProcessInactiveObjects> },
		static_cast<uintptr_t>(x.value),
		JobSystem::Priority::HIGH,
		m_PhaseCounter
	});
}
void Gamestate::AddToCleanupObjectsDelayed(std::shared_ptr<GameObject> obj)
{
	m_ObjectsToCleanUp[ThreadIndex].push_back(obj);
	auto x = JobData_Indices(ThreadIndex, static_cast<uint16_t>(m_ObjectsToCleanUp[ThreadIndex].size() - 1));
	JobSystem::AddJobToDelayedBuffer({
	{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::ProcessInactiveObjects> },
		static_cast<uintptr_t>(x.value),
		JobSystem::Priority::HIGH,
		m_PhaseCounter
	});
}

void Gamestate::MakeUpdateJobData()
{
	int numObjects = m_AllActiveGameObjects.size();
	int numThreads = std::min(NUM_THREADS, numObjects);
	int baseInterval = numObjects / numThreads;
	int remainingObjects = numObjects % numThreads;

	GameObjectMultiset::iterator startItr = m_AllActiveGameObjects.begin();
	GameObjectMultiset::iterator endItr = startItr;
		
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		if (startItr == m_AllActiveGameObjects.end())
		{
			m_JobIterators[i] = { m_AllActiveGameObjects.end(), m_AllActiveGameObjects.end() };
		}
		else
		{
			int interval = baseInterval;
			if (i < remainingObjects)
			{
				interval++;
			}
			for (int i = 0; i < interval; ++i)
			{
				++endItr;
			}
			m_JobIterators[i] = { startItr, endItr };
			startItr = endItr;
		}
	}
}
void Gamestate::MakeCollisionJobData()
{
	int numCells = GRID_RESOLUTION * GRID_RESOLUTION;
	int numThreads = std::min(NUM_THREADS, numCells);
	int baseInterval = numCells / numThreads;
	int remainingObjects = numCells % numThreads;

	int startIndex = 0;
	int endIndex = 0;
	for (int i = 0; i < numThreads; ++i)
	{
		int interval = baseInterval;
		if (i < remainingObjects)
		{
			interval++;
		}

		endIndex = startIndex + interval - 1;
		m_JobInts[i] = { startIndex, endIndex };
		startIndex = endIndex + 1;
	}
}
void Gamestate::UpdateGameObjectSection(uintptr_t pData)
{
	JobData_Iterators* data = reinterpret_cast<JobData_Iterators*>(pData);
	GameObjectMultiset::iterator startItr = data->start;
	GameObjectMultiset::iterator endItr = data->end;
	while(startItr != endItr)
	{
		(*startItr++)->Update(m_DeltaTime);
	}
}

void Gamestate::CreateUpdateJobs()
{
	JobPrepataionData prepData(NUM_THREADS+1, m_PhaseCounter);
	//prepData.Counter->count = NUM_THREADS+1;
	MakeUpdateJobData();
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		prepData.Declarations[i] = {
			{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::UpdateGameObjectSection> },
			reinterpret_cast<uintptr_t>(&m_JobIterators[i]),
			JobSystem::Priority::HIGH,
			prepData.Counter
		};
	}
	prepData.Declarations[NUM_THREADS] = {
		{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::Update> },
		0,
		JobSystem::Priority::HIGH,
		prepData.Counter
	};
	m_JobPrepData.push_back(std::make_unique<JobPrepataionData>(prepData));
	m_JobPhaseTransitions.push_back(std::make_unique<ThreadPhaseTransitionData>(&m_JobPrepData.back()->Declarations, false));
}
void Gamestate::CreateCollisionJobs()
{
	JobPrepataionData prepData(NUM_THREADS, m_PhaseCounter);
	MakeCollisionJobData();
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		prepData.Declarations[i] = {
			{ m_CollisionGrid.get(), &JobSystem::MemberFunctionDispatcher<ObjectCollisionGrid, &ObjectCollisionGrid::ResolveCollisionsOfCells> },
			reinterpret_cast<uintptr_t>(&m_JobInts[i]),
			JobSystem::Priority::HIGH,
			prepData.Counter
		};
	}
	m_JobPrepData.push_back(std::make_unique<JobPrepataionData>(prepData));
	m_JobPhaseTransitions.push_back(std::make_unique<ThreadPhaseTransitionData>(&m_JobPrepData.back()->Declarations, true));
}
void Gamestate::CreateCleanupJobs()
{
	JobPrepataionData prepData(1, m_PhaseCounter);

	prepData.Declarations[0] = {
		{ m_CollisionGrid.get(), &JobSystem::MemberFunctionDispatcher<ObjectCollisionGrid, &ObjectCollisionGrid::ClearFrameCollisionPairs>},
		0,
		JobSystem::Priority::HIGH,
		prepData.Counter
	};

	m_JobPrepData.push_back(std::make_unique<JobPrepataionData>(prepData));
	m_JobPhaseTransitions.push_back(std::make_unique<ThreadPhaseTransitionData>(&m_JobPrepData.back()->Declarations, true));
}
void Gamestate::CreateSnapshotJobs()
{
	JobPrepataionData prepData(NUM_THREADS, m_PhaseCounter);
	MakeUpdateJobData();
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		prepData.Declarations[i] = {
			{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::CreateSnapshotForGameObjectSection> },
			reinterpret_cast<uintptr_t>(&m_JobIterators[i]),
			JobSystem::Priority::HIGH,
			prepData.Counter
		};
	}
	m_JobPrepData.push_back(std::make_unique<JobPrepataionData>(prepData));
	m_JobPhaseTransitions.push_back(std::make_unique<ThreadPhaseTransitionData>(&m_JobPrepData.back()->Declarations, true));
}

void Gamestate::CreateUpkeepJobs()
{
	JobSystem::AddJobToUpkeep({
		{ m_PoolManager.get(), &JobSystem::MemberFunctionDispatcher<ObjectPoolManager, &ObjectPoolManager::MaintainPoolBuffers>},
		0,
		JobSystem::Priority::LOW,
		nullptr
	});
}

void Gamestate::ManageThreadPhaseTransition(uintptr_t data)
{
	ThreadPhaseTransitionData* transitionData = reinterpret_cast<pTPTD*>(data)->pData;
	m_CurrentTransitionData = m_JobPhaseTransitions[IncrementAndGetPhaseIndex()].get();
	JobSystem::AddJobsToBuffer(*(transitionData->NextDeclarations));
	JobSystem::AddJobToBuffer(m_PhaseTransitionDecl);
	JobSystem::WaitForCounterAndSwapBuffers(m_PhaseCounter, transitionData->IncludeMainThread);
}

void Gamestate::CreatePhaseTransitionDeclaration()
{
	m_CurrentTransitionData = m_JobPhaseTransitions[GetPhaseIndex()].get();
	m_PhaseTransitionDecl = {
		{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::ManageThreadPhaseTransition> },
			reinterpret_cast<uintptr_t>(&m_CurrentTransitionData),
			JobSystem::Priority::HIGH,
			m_UnusedTransitionCounter
	};
}
void Gamestate::CreateSnapshotForGameObjectSection(uintptr_t pData)
{
	JobData_Iterators* data = reinterpret_cast<JobData_Iterators*>(pData);
	GameObjectMultiset::iterator startItr = data->start;
	GameObjectMultiset::iterator endItr = data->end;
	while (startItr != endItr)
	{
		(*startItr++)->CreateSnapshot();
	}
}
void Gamestate::BeginPlay()
{
	JobSystem::InitJobSystem(NUM_THREADS);
	m_PhaseCounter = JobSystem::AllocCounter();
	m_UnusedTransitionCounter = JobSystem::AllocCounter();

	InitialiseTextures();
	InitialiseScreenText();
	InitialisePlayer();
	InitialiseObjectPools();

	m_JobIterators.reserve(NUM_THREADS);
	m_JobInts.reserve(NUM_THREADS);
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		m_JobInts.emplace_back();
		m_JobIterators.emplace_back(m_AllActiveGameObjects.begin(), m_AllActiveGameObjects.begin());
	}
	
	CreateUpkeepJobs();
	CreateUpdateJobs();
	CreateCollisionJobs();
	CreateCleanupJobs();
	CreateSnapshotJobs();
	CreatePhaseTransitionDeclaration();

	// kickoff
	sf::RenderWindow window(sf::VideoMode(SCREEN_WIDTH, SCREEN_HEIGHT), "LHG Code Exercise");
	sf::Clock gameClock;

	m_PhaseCounter->count.store(1);
	JobSystem::KickJob(m_PhaseTransitionDecl);
	m_PhaseCounter->count.fetch_sub(1);

	while(m_Player->GetLives() >= 0 && window.isOpen())
	{
		// Get the delta time for the game update
		sf::Time dt = gameClock.restart();
		m_DeltaTime = dt.asSeconds();
		if (m_DeltaTime > 0.05f)
		{
			m_DeltaTime = 0.05f;
		}
		// Poll for window being closed
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
		}

		// Rendering (Other threads - Input & Update, then Collision Resolution)
		window.clear();
		Draw(window);
		window.display();
		while (GetPhaseIndex()!=3 || m_PhaseCounter->count.load() != 1 || JobSystem::IsBufferEmpty())
		{
			std::this_thread::yield();
		}
		m_PhaseCounter->count.fetch_sub(1);

		// Cleanup
		CleanUp();
		while (m_PhaseCounter->count.load() != 1 || JobSystem::IsBufferEmpty())
		{
			std::this_thread::yield();
		}
		m_PhaseCounter->count.fetch_sub(1);

		// Snapshot
		for (size_t i{ 0 }; i < m_ObjectsToCleanUp.size(); ++i)
		{
			m_ObjectsToCleanUp[i].clear();
		}
		while (m_PhaseCounter->count.load() != 1 || JobSystem::IsBufferEmpty())
		{
			std::this_thread::yield();
		}
		if (m_Player->GetLives() < 0) break;
		m_PhaseCounter->count.fetch_sub(1);
	}
	std::cout << "FINAL SCORE: " << GetScore();

	JobSystem::ClearBuffer();
	m_PhaseCounter->count.fetch_sub(1);

	JobSystem::FreeCounter(m_PhaseCounter);
	JobSystem::FreeCounter(m_UnusedTransitionCounter);
	JobSystem::ShutdownJobSystem();
}


void Gamestate::CleanUp()
{
	int sizeChanged = 0;
	for (size_t i{ 0 }; i < m_ObjectsToCleanUp.size(); ++i)
	{
		for (size_t j{ 0 }; j < m_ObjectsToCleanUp[i].size(); ++j)
		{
			++sizeChanged;
			m_AllActiveGameObjects.erase(m_ObjectsToCleanUp[i][j]);
		}
	}
	for (size_t i{ 0 }; i < m_ObjectsToAdd.size(); ++i)
	{
		for (size_t j{ 0 }; j < m_ObjectsToAdd[i].size(); ++j)
		{
			++sizeChanged;
			m_AllActiveGameObjects.insert(m_ObjectsToAdd[i][j]);
		}
		m_ObjectsToAdd[i].clear();
	}
	if (sizeChanged)
	{
		// remake chunks for dividing game objects for threads
		MakeUpdateJobData();
	}
}

std::shared_ptr<GameObject> Gamestate::GetPooledObject(const std::string& PoolName)
{
	return m_PoolManager->GetPooledObject(PoolName);
}

void Gamestate::ProcessInactiveObjects(uintptr_t data)
{
	JobData_Indices iData(data);
	auto& obj = m_ObjectsToCleanUp[iData.indices.first][iData.indices.second];
	CollisionComponent* collComp = obj->GetComponent<CollisionComponent>();
	if (collComp)
	{
		collComp->ClearFromGrid();
	}
	PooledObjectComponent* poolComp = obj->GetComponent<PooledObjectComponent>();
	if (poolComp)
	{
		poolComp->ReturnToPool(obj);
	}
}
