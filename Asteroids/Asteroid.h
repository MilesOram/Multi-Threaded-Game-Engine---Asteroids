#pragma once
#include "GameObject.h"

// enum for asteroid size
enum class AST_SIZE { small, medium, large };

// asteroids are spawned at the edge of the screen, they drift and rotate and wrap around the screen
// on collision with player-fired projectile, they large splits into two medium, medium into two smalls and small is destroyed 
// (all are returned to object pools when inactive) +10 score for each asteroid destroyed, damages player if hit
class Asteroid : public GameObject
{
private:
	// rotation and drift, on creation, drifts inwards on screen
	float m_RotationSpeed = 40;
	float m_XDrift = 0;
	float m_YDrift = 0;
	float m_Lifetime = 0;
	const int m_Score = 10;
	AST_SIZE m_Size = AST_SIZE::large;
public:
	Asteroid(sf::Texture& _Texture, AST_SIZE _Size);
	Asteroid(const Asteroid& other);
	// overrides
	void Update(float deltaTime) override;
	void HandleCollision(uint16_t otherTags) override;
	void Reinitialise() override;
	std::shared_ptr<GameObject> CloneToSharedPtr() override;
	// called on successful collision with projectile
	void Split();
	// large spawns two mediums (retrieve from pool)
	void SpawnMediums() const;
	// mediums spawns two smalls (retrieve from pool)
	void SpawnSmalls() const;

	void SetDrift(float x, float y) { m_XDrift = x; m_YDrift = y; }
	void SetSize(AST_SIZE size) { m_Size = size; }

	static const char AsteroidSmallPoolName[14];
	static const char AsteroidMediumPoolName[15];
	static const char AsteroidLargePoolName[14];
	static const uint16_t DefaultCollisionTagsSelf;
	static const uint16_t DefaultCollisionTagsOther;
};