#pragma once
#include "GameObject.h"

// player controlled ship, fires projectiles and is damaged by asteroids, controlled with arrow keys to rotate and accelerate/decelerate
// velocity decays over time, respawns in middle after hit and loses a life, then invulnerable for a period,
// while flying, wraps around screen 
class PlayerShip : public GameObject
{
private:
	int m_Lives = 3;
	float m_VelX = 0;
	float m_VelY = 0;
	float m_ProjectileCD = .25f;
	float m_TimeSincelastProjectile = 0;
	float m_TimeSinceInvulnBegin = 0;
	const float m_Acceleration = 480.f;
	const float m_MaxSpeed = 400.f;
	const float m_RotationSpeed = 170.f;
	const float m_DecayRate = 30.0f;
	const float m_InvulnTimer = 3.f;

	// processes inputs, called at the start of update
	void HandleInputs(float deltaTime);
	// fires projectile in the forward direction
	void FireProjectile();
	void RotateLeft(float);
	void RotateRight(float);
	void AccelerateForward(float);
	void Decelerate(float);
	// velocity decays by a fixed amount
	void Decay(float);
public:
	PlayerShip(sf::Texture& _Texture);
	PlayerShip(const PlayerShip&);
	// overrides
	void Update(float deltaTime) override;
	void HandleCollision(uint16_t otherTags) override;
	std::shared_ptr<GameObject> CloneToSharedPtr() override;
	void Reinitialise() override {}

	void SetProjectileCD(float cd) { m_ProjectileCD = cd; }
	int GetLives() const { return m_Lives; }

	static const uint16_t DefaultCollisionTagsSelf;
	static const uint16_t DefaultCollisionTagsOther;
	const char ProjectilePoolName[15] = "ProjectilePool";
};


