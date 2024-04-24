#pragma once
#include "Gamestate.h"
#include "GameObject.h"
#include "Asteroid.h"
#include "Projectile.h"
#include "PlayerShip.h"
#include "Components.h"

void Gamestate::InitialisePlayer()
{
	m_Player = std::make_shared<PlayerShip>(ObjectTextures[0]);
	std::shared_ptr<GameObject> player = m_Player;
	Polygon verts = { {-10.f, 8.6f},{0.f, 17.3f},{10.f, 8.6f},{10.f, -8.6f},{0, -17.3f},{ -10.f, -8.6f } };
	player->AddComponent<PolygonCollisionComponent>(player, verts, PlayerShip::DefaultCollisionTagsSelf, PlayerShip::DefaultCollisionTagsOther);
	m_AllActiveGameObjects.insert(m_Player);
}
void Gamestate::InitialiseObjectPools()
{
	/*
	Player: can be damaged by asteroids
	Projectile: can damage asteroids, can be damaged by asteroids
	Asteroids: can damage projectiles and player, can be damaged by projectiles

	first flag = can be damaged by 'enemy damage'
	second flag = can deal 'enemy damage'
	thrid flag = can be damaged by 'player damage'
	fourth flag = can deal 'player damage'

				self.			   lookfor.			v this bit is reserved for use by the collision grid, so up to 15 unique tags can be used
	Player:     0b1000000000000000 0b0100000000000000
	Projectile: 0b1001000000000000 0b0110000000000000
	Asteroids:  0b0110000000000000 0b1001000000000000

	*/
	
	// projectile prefab
	std::shared_ptr<GameObject> projectileBase = std::make_shared<Projectile>(ObjectTextures[1]);
	projectileBase->AddComponent<PooledObjectComponent>(projectileBase, nullptr);
	projectileBase->AddComponent<CircleCollisionComponent>(projectileBase, 5.f, Projectile::DefaultCollisionTagsSelf, Projectile::DefaultCollisionTagsOther);
	// make pool - sets the pool pointer in the pooledobjectcomponent
	m_PoolManager->CreatePool(m_Player->ProjectilePoolName, projectileBase, 3, 10, .5f, 1.f);

	// large asteroid prefab
	std::shared_ptr<GameObject> asteroidLarge = std::make_shared<Asteroid>(ObjectTextures[2], AST_SIZE::large);
	asteroidLarge->AddComponent<PooledObjectComponent>(asteroidLarge, nullptr);
	Polygon verts = { {69.28f,40.f},{0,80.f},{-69.28f,40.f},{-69.28f,-40.f},{0,-80.f},{69.28f,-40.f} };
	asteroidLarge->AddComponent<PolygonCollisionComponent>(asteroidLarge, verts, Asteroid::DefaultCollisionTagsSelf, Asteroid::DefaultCollisionTagsOther);
	// make pool
	m_PoolManager->CreatePool(Asteroid::AsteroidLargePoolName, asteroidLarge, 3, 10, .5f, 1.f);

	// medium asteroid prefab
	std::shared_ptr<GameObject> asteroidMedium = std::make_shared<Asteroid>(ObjectTextures[3], AST_SIZE::medium);
	asteroidMedium->AddComponent<PooledObjectComponent>(asteroidMedium, nullptr);
	asteroidMedium->AddComponent<BoxCollisionComponent>(asteroidMedium, 45.f, 45.f, Asteroid::DefaultCollisionTagsSelf, Asteroid::DefaultCollisionTagsOther);
	m_PoolManager->CreatePool(Asteroid::AsteroidMediumPoolName, asteroidMedium, 3, 10, .5f, 1.f);

	// small asteroid prefab
	std::shared_ptr<GameObject> asteroidSmall = std::make_shared<Asteroid>(ObjectTextures[4], AST_SIZE::small);
	asteroidSmall->AddComponent<PooledObjectComponent>(asteroidSmall, nullptr);
	asteroidSmall->AddComponent<CircleCollisionComponent>(asteroidSmall, 20.f, Asteroid::DefaultCollisionTagsSelf, Asteroid::DefaultCollisionTagsOther);
	// make pool
	m_PoolManager->CreatePool(Asteroid::AsteroidSmallPoolName, asteroidSmall, 3, 10, .5f, 1.f);
}