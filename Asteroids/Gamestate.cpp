#include "Gamestate.h"
#include "ObjectPool.h"
#include "Components.h"
#include "PlayerShip.h"
#include "JobSystem.h"
#include "GameObject.h"
#include "Asteroid.h"
#include "Projectile.h"
#include "CollisionGrid.h"
#include "Particles.h"
#include <immintrin.h>

thread_local int ThreadIndex;
Gamestate* Gamestate::instance{ nullptr };

bool TextureAndIDComparator::operator()(const std::shared_ptr<GameObject>& lhs, const std::shared_ptr<GameObject>& rhs) const
{
	if (lhs->GetSprite().getTexture() != rhs->GetSprite().getTexture())
	{
		return lhs->GetSprite().getTexture() < rhs->GetSprite().getTexture();
	}
	return lhs->getId() < rhs->getId();
}

int random_int(int range_lower, int range_upper)
{
	if (range_lower > range_upper) throw std::out_of_range("rand int bad");
	if (range_upper == range_lower) return range_lower;
	std::random_device rd;
	std::mt19937 rng(rd());
	std::uniform_int_distribution<std::mt19937::result_type> rnd_dist(range_lower, range_upper);
	return rnd_dist(rng);
}

Gamestate::Gamestate()
{
	if (instance == nullptr) instance = this;
	m_PoolManager = std::make_shared<ObjectPoolManager>();
	m_CollisionGrid = std::make_shared<ObjectCollisionGrid>();

	m_ObjectsToAdd = std::vector<std::vector<std::shared_ptr<GameObject>>>(NUM_THREADS, std::vector<std::shared_ptr<GameObject>>());
	m_ObjectsToCleanUp = std::vector<std::vector<std::shared_ptr<GameObject>>>(NUM_THREADS, std::vector<std::shared_ptr<GameObject>>());

#if USE_CPU_FOR_OCCLUDERS
	m_PixelPrep = (int*)std::malloc(SCREEN_HEIGHT * SCREEN_WIDTH * sizeof(int));
	if (m_PixelPrep != NULL)
	{
		for (int i = 0; i < SCREEN_HEIGHT * SCREEN_WIDTH; ++i)
		{
			m_PixelPrep[i] = 0;
		}
	}
#endif

	m_PhaseCounter = JobSystem::AllocCounter();
	m_UnusedTransitionCounter = JobSystem::AllocCounter();
	m_ParticleSystem = std::make_shared<ParticleSystem>(2000);

	m_CurrentTransitionData = nullptr;
	m_PhaseTransitionDecl = { nullptr, nullptr, 0, JobSystem::Priority::NORMAL, nullptr };
}
Gamestate::~Gamestate()
{
#if USE_CPU_FOR_OCCLUDERS
	std::free(m_PixelPrep);
#endif

	JobSystem::FreeCounter(m_PhaseCounter);
	JobSystem::FreeCounter(m_UnusedTransitionCounter);
}

// update for asteroids spawning and overdrive
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
		m_AsteroidCD = m_OverdriveAsteroidCD;
		m_Player->SetProjectileCD(m_OverdriveProjectileCD);
		m_OverdriveTimer = 0;
		m_Overdrive = true;
		m_GlowColourChange = GlowColourChange::RED;
	}
	else if (m_OverdriveTimer > 10 && m_Overdrive)
	{
		m_AsteroidCD = m_BaseAsteroidCD;
		m_Player->SetProjectileCD(m_BaseProjectileCD);
		m_OverdriveTimer = 0;
		m_Overdrive = false;
		m_GlowColourChange = GlowColourChange::BLUE;
	}
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
	ObjectTextures = std::vector<sf::Texture>(6);
	ObjectTextures[0].loadFromFile("Assets/Ship.png");
	ObjectTextures[1].loadFromFile("Assets/Projectile.png");
	ObjectTextures[2].loadFromFile("Assets/AsteroidL.png");
	ObjectTextures[3].loadFromFile("Assets/AsteroidM.png");
	ObjectTextures[4].loadFromFile("Assets/AsteroidS.png");
	ObjectTextures[5].loadFromFile("Assets/texture_atlas.png");

#if USE_CPU_FOR_OCCLUDERS
	m_MainTexture.create(SCREEN_WIDTH, SCREEN_HEIGHT);
#endif

}
void Gamestate::InitialiseShaders()
{

	if (!m_BlurShader.loadFromFile("blur.vert", sf::Shader::Vertex) ||
		!m_BlurShader.loadFromFile("blur.frag", sf::Shader::Fragment))
	{
		return;
	}

#if USE_CPU_FOR_OCCLUDERS
	if (!m_FogShader.loadFromFile("fog.vert", sf::Shader::Vertex) ||
		!m_FogShader.loadFromFile("altFog.frag", sf::Shader::Fragment))
	{
		return;
	}
	m_FogShader.setUniform("maxDist", m_NearFogRadius);
	if (!m_GlowShader.loadFromFile("glow.vert", sf::Shader::Vertex) ||
		!m_GlowShader.loadFromFile("altGlow.frag", sf::Shader::Fragment))
	{
		return;
	}
#else
	if (!m_FogShader.loadFromFile("fog.vert", sf::Shader::Vertex) ||
		!m_FogShader.loadFromFile("fog.frag", sf::Shader::Fragment))
	{
		return;
	}
	if (!m_GlowShader.loadFromFile("glow.vert", sf::Shader::Vertex) ||
		!m_GlowShader.loadFromFile("glow.frag", sf::Shader::Fragment))
	{
		return;
	}
	m_GlowShader.setUniform("radialPulseWidth", m_PulseWidth);
	m_GlowShader.setUniform("centreForLastPulse", m_Player->GetPosition());
#endif
	if (!m_LightenShader.loadFromFile("uniqueColour.vert", sf::Shader::Vertex) ||
		!m_LightenShader.loadFromFile("uniqueColour.frag", sf::Shader::Fragment))
	{
		return;
	}

	m_FogShader.setUniform("windowSize", sf::Vector2f(SCREEN_WIDTH, SCREEN_HEIGHT));
	m_BlurShader.setUniform("texSize", sf::Vector2f(250, 250));

	// Set the shader parameters
	m_GlowShader.setUniform("glowColor", m_BaseGlowColor);
	m_GlowShader.setUniform("shipPosition", m_Player->GetPosition());
	m_GlowShader.setUniform("glowRadius", m_LowGlowRadius);
	m_GlowShader.setUniform("windowSize", sf::Vector2f(SCREEN_WIDTH, SCREEN_HEIGHT));
	m_GlowShader.setUniform("gridResolution", sf::Vector2f(SCREEN_WIDTH, SCREEN_HEIGHT));
	
	m_FullScreenQuad = sf::VertexArray(sf::Quads);
	m_FullScreenQuad.append(sf::Vertex(sf::Vector2f(0, 0)));
	m_FullScreenQuad.append(sf::Vertex(sf::Vector2f(SCREEN_WIDTH, 0)));
	m_FullScreenQuad.append(sf::Vertex(sf::Vector2f(SCREEN_WIDTH, SCREEN_HEIGHT)));
	m_FullScreenQuad.append(sf::Vertex(sf::Vector2f(0, SCREEN_HEIGHT)));
}


#if USE_CPU_FOR_OCCLUDERS
void Gamestate::Draw(sf::RenderWindow& window)
{
	m_BufferRenderTexture1.clear();

	m_GlowShader.setUniform("shipPosition", m_Player->GetPosition());
	m_GlowShader.setUniform("occupancyTex", m_MainTexture);
	m_BufferRenderTexture1.draw(m_FullScreenQuad, &m_GlowShader);
	m_BufferRenderTexture1.display();

	m_BufferRenderTexture2.clear();

	sf::Sprite sp1(m_BufferRenderTexture1.getTexture());
	m_BlurShader.setUniform("texture", m_BufferRenderTexture1.getTexture());
	m_BufferRenderTexture2.draw(sp1, &m_BlurShader);
	m_BufferRenderTexture2.display();

	window.clear();

	sf::Sprite sp2(m_BufferRenderTexture2.getTexture());
	m_BlurShader.setUniform("texture", m_BufferRenderTexture2.getTexture());

	m_FogShader.setUniform("center", m_Player->GetPosition());

	window.draw(sp1, &m_BlurShader);
	
	// Draw all Game Objects
	for (auto& obj : m_AllActiveGameObjects)
	{
		window.draw(obj->GetSprite(), &m_FogShader);
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
#else
// utility functions for asteroid vertex array
void Gamestate::RotateBox(float* coords, float sinAngle, float cosAngle)
{
	// Load the coordinates into SIMD registers
	__m128 x = _mm_loadu_ps(coords);
	__m128 y = _mm_loadu_ps(coords + 4);

	float s = sinAngle;
	float c = cosAngle;

	__m128 sin_vec = _mm_set1_ps(s);
	__m128 cos_vec = _mm_set1_ps(c);

	__m128 x1 = _mm_sub_ps(_mm_mul_ps(x, cos_vec), _mm_mul_ps(y, sin_vec));
	__m128 y1 = _mm_add_ps(_mm_mul_ps(x, sin_vec), _mm_mul_ps(y, cos_vec));

	// Store the rotated coordinates back into the array
	_mm_storeu_ps(coords, x1);
	_mm_storeu_ps(coords + 4, y1);
}
float Gamestate::CalculateDistanceSquared(const sf::Vector2f& point1, const sf::Vector2f& point2)
{
	float dx = point1.x - point2.x;
	float dy = point1.y - point2.y;
	return dx * dx + dy * dy;
}

void Gamestate::SortObjectsByDistance(std::vector<std::pair<std::shared_ptr<GameObject>, float>>& objs) 
{
	// Sort in descending order so that closer objects are rendered last
	std::sort(objs.begin(), objs.end(), [](const std::pair<std::shared_ptr<GameObject>, float>& a, const std::pair<std::shared_ptr<GameObject>, float>& b) 
	{
		return a.second > b.second; 
	});
}
void Gamestate::DrawAsteroidVertexArray(sf::VertexArray& vertices, sf::RenderStates& states, std::vector<std::pair<std::shared_ptr<GameObject>, float>>& objs, sf::RenderTexture& tex, sf::Shader& shader)
{
	SortObjectsByDistance(objs);
	const int spriteCount = objs.size();

	const sf::Texture* texture = objs[0].first->GetSprite().getTexture();

	for (int i = 0; i < spriteCount; ++i)
	{
		sf::Vector2f atlasOffsetTL = objs[i].first->GetTextureAtlasOffsetTL();
		sf::Vector2f atlasOffsetBR = objs[i].first->GetTextureAtlasOffsetBR();

		float halfWidth = (atlasOffsetBR.x - atlasOffsetTL.x)/2;
		float halfHeight = (atlasOffsetBR.y - atlasOffsetTL.y)/2;

		float coords[8] = {-halfWidth,halfWidth,halfWidth,-halfWidth, -halfHeight,-halfHeight,halfHeight,halfHeight};
		float angle = objs[i].first->GetSprite().getRotation();

		RotateBox(coords, std::sin(angle * TO_RADIANS), std::cos(angle * TO_RADIANS));

		sf::Vector2f position = objs[i].first->GetSprite().getPosition();

		sf::Color color(255 * (i + 1) / spriteCount,255, 0);
		sf::Vertex quad[4];
		// Top-left
		quad[0].position = {coords[0] + position.x, coords[4] + position.y};
		quad[0].texCoords = atlasOffsetTL;
		quad[0].color = color;

		// Top-right
		quad[1].position = { coords[1] + position.x, coords[5] + position.y };
		quad[1].texCoords = sf::Vector2f(atlasOffsetBR.x, atlasOffsetTL.y);
		quad[1].color = color;

		// Bottom-right
		quad[2].position = { coords[2] + position.x, coords[6] + position.y };
		quad[2].texCoords = atlasOffsetBR;
		quad[2].color = color;

		// Bottom-left
		quad[3].position = { coords[3] + position.x, coords[7] + position.y };
		quad[3].texCoords = sf::Vector2f(atlasOffsetTL.x, atlasOffsetBR.y );
		quad[3].color = color;

		// Append the quad to the vertex array
		for (int j = 0; j < 4; ++j) 
		{
			vertices.append(quad[j]);
		}
	}
	states.texture = texture;
	states.shader = &shader;
	tex.draw(vertices, states);
}
void Gamestate::Draw(sf::RenderWindow& window)
{
	// clear previous
	window.clear();
	m_BufferRenderTexture1.clear();
	m_BufferRenderTexture2.clear();

	// reset pulse if off screen
	if (m_PulsePosition > SCREEN_WIDTH)
	{
		m_PulsePosition = -1.f;
		m_GlowShader.setUniform("centreForLastPulse", m_LastShipPos);
	}

	// update pulse position
	m_PulsePosition += m_DeltaTime * m_PulseSpeed;
	m_GlowShader.setUniform("radialPulseStart", m_PulsePosition);

	// ideally should be storing a vertex array and updating it each frame instead of making a new one
	sf::VertexArray vertices(sf::Quads);
	sf::RenderStates states;

	// need occluders to be rendered in order, starting with closest to player, for shadows to look ok
	std::vector<std::pair<std::shared_ptr<GameObject>, float>> occludersWithDistanceToPlayer;
	for (auto& obj : m_AllActiveGameObjects)
	{
		if (obj->GetOccluder())
		{
			occludersWithDistanceToPlayer.push_back({ obj, CalculateDistanceSquared(obj->GetSprite().getPosition(), m_LastShipPos) });
		}
	}
	if (!occludersWithDistanceToPlayer.empty())
	{
		// adds all asteroids to a vertex array, draws each with a unique solid colour, gives an effective occluder/occupancy texture
		DrawAsteroidVertexArray(vertices, states, occludersWithDistanceToPlayer, m_BufferRenderTexture1, m_LightenShader);
	}

	// update uniform if needed
	if (m_SubstantialShipMovement)
	{
		m_GlowShader.setUniform("shipPosition", m_LastShipPos);
	}
	// m_BufferRenderTexture1 is now used an an occupancy texture for line tracing the glow and pulse
	m_GlowShader.setUniform("occupancyTex", m_BufferRenderTexture1.getTexture());

	// draw glow (and pulse) to a second buffer texture
	m_BufferRenderTexture2.draw(m_FullScreenQuad, &m_GlowShader);
	m_BufferRenderTexture2.display();

	// draw glow to main window and apply blur
	sf::Sprite sp(m_BufferRenderTexture2.getTexture());
	window.draw(sp, &m_BlurShader);

	// might seem convoluted, but the unique colours ealier are important here
	// in order for asteroids to be lit, their interior must be fully visible in this alpha map and hence the glow must go inside each asteroid it reaches
	// ideally your line trace stops at the first boundary, but here it needs to go through only 2 asteroids (looks better with 2 than 1) and no more
	// the unique colours allow the trace to stop once it reaches a third asteroid or once it exits for long enough (see shader for details)
	m_FogShader.setUniform("alphaMap", m_BufferRenderTexture2.getTexture());

	// draw all objects non occluders normally
	for (auto& obj : m_AllActiveGameObjects)
	{
		if (!obj->GetOccluder())
		{
			window.draw(obj->GetSprite());
		}
	}
	// draw asteroids still stored in the vertex array using the fog shader
	states.shader = &m_FogShader;
	window.draw(vertices, states);

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
#endif
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

void Gamestate::UpdateParticleSystem(uintptr_t data)
{
	JobData_Indices iData(data);
	float angle = m_Player->GetSprite().getRotation() - 270;
	int offsetMult = 25;
	if (iData.indices.first == 0)
	{
		m_ParticleSystem->SetEmitter(m_Player->GetSprite().getPosition() + sf::Vector2f(std::cos(angle* TO_RADIANS)* offsetMult, std::sin(angle*TO_RADIANS)* offsetMult));
	}
	m_ParticleSystem->Update(m_Elapsed, iData.indices.first, iData.indices.second,angle);
}


void Gamestate::CreateUpdateJobs()
{
	int particleJobs = 4;
	JobPrepataionData prepData(NUM_THREADS + 1 + particleJobs, m_PhaseCounter);
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

	int interval = m_ParticleSystem->GetParticleCount() / particleJobs;
	assert(m_ParticleSystem->GetParticleCount() % particleJobs == 0);

	for (int i = NUM_THREADS + 1; i < NUM_THREADS + 1 + particleJobs; ++i)
	{
		auto x = JobData_Indices((i - NUM_THREADS - 1) * interval, (i - NUM_THREADS) * interval - 1);
		prepData.Declarations[i] = {
			{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::UpdateParticleSystem> },
			static_cast<uintptr_t>(x.value),
			JobSystem::Priority::HIGH,
			prepData.Counter
		};
	}
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
	JobPrepataionData prepData(1 , m_PhaseCounter);

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

	JobSystem::SetIncludeMainThread(transitionData->IncludeMainThread);
	JobSystem::AddJobsToBuffer(*(transitionData->NextDeclarations));
	JobSystem::AddJobToBuffer(m_PhaseTransitionDecl);
}

void Gamestate::CreatePhaseTransitionDeclaration()
{
	m_CurrentTransitionData = m_JobPhaseTransitions[GetPhaseIndex()].get();
	m_PhaseTransitionDecl = {
		{ instance, &JobSystem::MemberFunctionDispatcher<Gamestate, &Gamestate::ManageThreadPhaseTransition> },
			reinterpret_cast<uintptr_t>(&m_CurrentTransitionData),
			JobSystem::Priority::HIGH,
			m_PhaseCounter
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
	// initialise the job system
	JobSystem::InitJobSystem(NUM_THREADS);

	InitialiseTextures();
	InitialiseScreenText();
	InitialisePlayer();
	InitialiseObjectPools();
	InitialiseShaders();
	
	// create buffer textures used in rendering process
	m_BufferRenderTexture1.create(SCREEN_WIDTH, SCREEN_HEIGHT);
	m_BufferRenderTexture2.create(SCREEN_WIDTH, SCREEN_HEIGHT);

	// setup job data with default values
	m_JobIterators.reserve(NUM_THREADS);
	m_JobInts.reserve(NUM_THREADS);
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		m_JobInts.emplace_back();
		m_JobIterators.emplace_back(m_AllActiveGameObjects.begin(), m_AllActiveGameObjects.begin());
	}
	
	// create all repeated jobs and job data
	CreateUpkeepJobs();
	CreateUpdateJobs();
	CreateCollisionJobs();
	CreateCleanupJobs();
	CreateSnapshotJobs();
	CreatePhaseTransitionDeclaration();

	// kickoff
	sf::RenderWindow window(sf::VideoMode(SCREEN_WIDTH, SCREEN_HEIGHT), "");
	m_PhaseCounter->count.store(0);
	// call the phase transition job a single time to begin
	m_PhaseTransitionDecl.m_MemberFunction.func(m_PhaseTransitionDecl.m_MemberFunction.instance, m_PhaseTransitionDecl.m_Param);
	JobSystem::NextPhase(m_PhaseCounter);
	
	// Main game loop
	while(window.isOpen())
	{
		// Check ship position for significant change to decide whether to update shader uniforms
		CheckShipPosition();
		// Set the delta time for the game update
		RestartClock();

		// Poll for window being closed
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
		}

		// Rendering, Input & Update -------------------------------
		// Main thread: Create vertex array for asteroids, draw all but particles, complete glow, blur and fog effects
		// Other threads: Handle inputs, then update each game object and update collision grid with new positions, update particle system
		Draw(window);

		// ensure main thread doesn't get in between the two phases used by the other threads (this very rarely incurs any wait time since
		// Draw(window) almost always takes longer than the input/update phase of other threads)
		while (GetPhaseIndex()!=3)
		{
			std::this_thread::yield();
		}

		// More Rendering, Collision Resolution --------------------
		// Main thread: Draw particle system, display window
		// Other threads: Test intersections and resolve collisions
		window.draw(*m_ParticleSystem);
		window.display();
		SyncWithOtherThreads();

		// Cleanup -------------------------------------------------
		// Main thread: Handle added or removed objects which were queued during previous phases
		// Other threads: Clear collision grid pair tracker
		CleanUp();
		SyncWithOtherThreads();

		// Snapshot ------------------------------------------------
		// Main thread: Clear temp object containers and check for glow changes queued in earlier phases and update shaders
		// Other threads: Set the rot and pos of the sprite in each game object to be used as a snapshot for drawing next frame
		ClearCleanUpObjects();
		CheckGlowShaderUniforms();
	#if USE_CPU_FOR_OCCLUDERS
		// update the texture whilst the other threads create the snapshot, only needed for alternate glow method
		m_MainTexture.update(reinterpret_cast<sf::Uint8*>(m_PixelPrep));
	#endif
		if (m_Player->GetLives() < 0) break;
		SyncWithOtherThreads();

		sf::Time frameTime = m_GameClock.getElapsedTime();

		// If the frame completed early, sleep for the remaining time
		if (frameTime < MIN_FRAME_TIME)
		{
			sf::sleep(MIN_FRAME_TIME - frameTime);
		}
	}
	std::cout << "FINAL SCORE: " << GetScore();

	JobSystem::ClearBuffer();
	m_PhaseCounter->count.fetch_sub(1);

	JobSystem::ShutdownJobSystem();
}
void Gamestate::SyncWithOtherThreads()
{
	while (m_PhaseCounter->count.load() != 1 || JobSystem::IsBufferEmpty())
	{
		std::this_thread::yield();
	}
	m_PhaseCounter->count.fetch_sub(1);
	JobSystem::NextPhase(m_PhaseCounter);
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

#if USE_CPU_FOR_OCCLUDERS
int* Gamestate::GetPixelPrepPtr(int index)
{
	return m_PixelPrep + index;
}
#endif

void Gamestate::RestartClock()
{
	sf::Time dt = m_GameClock.restart();
	m_Elapsed = dt;
	m_DeltaTime = dt.asSeconds();
	if (m_DeltaTime > 0.033f)
	{
		m_DeltaTime = 0.033f;
	}
}

void Gamestate::SignalLightUp()
{ 
	m_GlowRadiusChange = GlowRadiusChange::FULL;
}
void Gamestate::SignalLightDown()
{
	m_GlowRadiusChange = GlowRadiusChange::HALF;
}

void Gamestate::CheckShipPosition()
{
	sf::Vector2f lastShipPos = m_Player->GetSprite().getPosition();
	float dx = m_LastShipPos.x - lastShipPos.x;
	float dy = m_LastShipPos.y - lastShipPos.y;
	if (dx * dx + dy * dy > 4.f)
	{
		m_SubstantialShipMovement = true;
		m_LastShipPos = lastShipPos;
	}
	else
	{
		m_SubstantialShipMovement = false;
	}
}

void Gamestate::ClearCleanUpObjects()
{
	for (size_t i{ 0 }; i < m_ObjectsToCleanUp.size(); ++i)
	{
		m_ObjectsToCleanUp[i].clear();
	}
}

void Gamestate::CheckGlowShaderUniforms()
{
	// I don't like checking this every frame but I don't have a job queue set up for the main thread, and the main thread needs to do this
	// the changes happen during the update phase and need to be enacted after the main thread is done rendering so they go here
	if (m_GlowColourChange != GlowColourChange::NOCHANGE)
	{
		switch (m_GlowColourChange)
		{
		case GlowColourChange::RED:
			m_GlowShader.setUniform("glowColor", m_OverdriveGlowColor);
			break;
		case GlowColourChange::BLUE:
			m_GlowShader.setUniform("glowColor", m_BaseGlowColor);
			break;
		}
		m_GlowColourChange = GlowColourChange::NOCHANGE;
	}
	if (m_GlowRadiusChange != GlowRadiusChange::NOCHANGE)
	{
		switch (m_GlowRadiusChange)
		{
		case GlowRadiusChange::FULL:
			m_GlowShader.setUniform("glowRadius", m_HighGlowRadius);
#if USE_CPU_FOR_OCCLUDERS
			m_FogShader.setUniform("maxDist", m_FarFogRadius);
#endif
			break;
		case GlowRadiusChange::HALF:
			m_GlowShader.setUniform("glowRadius", m_LowGlowRadius);
#if USE_CPU_FOR_OCCLUDERS
			m_FogShader.setUniform("maxDist", m_NearFogRadius);
#endif
			break;
		}
		m_GlowRadiusChange = GlowRadiusChange::NOCHANGE;
	}
}
