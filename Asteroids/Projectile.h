#pragma once
#include "GameObject.h"

// spawned by player ship, damages asteroids, is destroyed (returned to pool) on collision with asteroid or moving off-screen
class Projectile : public GameObject
{
private:
	float m_VelX = 0;
	float m_VelY = 0;
	const float m_Speed = 500;
	void CalculateXandYVelocity();

public:
	Projectile(sf::Texture& _Texture);
	Projectile(const Projectile& other);

	// overrides
	void Update(float deltaTime) override;
	void HandleCollision(uint16_t otherTags) override;
	void Reinitialise() override;
	std::shared_ptr<GameObject> CloneToSharedPtr() override;

	static const uint16_t DefaultCollisionTagsSelf;
	static const uint16_t DefaultCollisionTagsOther;
};