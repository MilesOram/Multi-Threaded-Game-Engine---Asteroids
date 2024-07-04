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

	// note that verts must go round anti-clockwise
	Polygon verts = { {-4.f, -15.f },{4.f, -15.f},{25.f, 0.f},{4.f, 15.f},{-4.f, 15.f},{-25.f, -0.f} };
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

				self.			   lookfor.		   v this bit is reserved for use by the collision grid (denotes edges of phase box)
												   |v this bit is reserved for use by the collision grid (denotes occupying more than one grid cell)
	Player:     0b1000000000000000 0b0100000000000000
	Projectile: 0b1001000000000000 0b0110000000000000
	Asteroids:  0b0110000000000000 0b1001000000000000

	14 availble tags (16-2)
	*/
	
	// projectile prefab
	std::shared_ptr<GameObject> projectileBase = std::make_shared<Projectile>(ObjectTextures[1]);
	projectileBase->AddComponent<PooledObjectComponent>(projectileBase, nullptr);
	projectileBase->AddComponent<CircleCollisionComponent>(projectileBase, 5.f, Projectile::DefaultCollisionTagsSelf, Projectile::DefaultCollisionTagsOther);
	// make pool - sets the pool pointer in the pooledobjectcomponent
	m_PoolManager->CreatePool(m_Player->ProjectilePoolName, projectileBase, 3, 10, .5f, 1.f);

#if USE_CPU_FOR_OCCLUDERS
	// not setup to use texture atlas
	int astSmallTexIndex = 4;
	int astMediumTexIndex = 3;
	int astLargeTexIndex = 2;
#else
	// all use texture atlas
	int astSmallTexIndex = 5;
	int astMediumTexIndex = 5;
	int astLargeTexIndex = 5;
#endif
	// large asteroid prefab
	std::shared_ptr<GameObject> asteroidLarge = std::make_shared<Asteroid>(ObjectTextures[astLargeTexIndex], AST_SIZE::large);
	asteroidLarge->AddComponent<PooledObjectComponent>(asteroidLarge, nullptr);
	Polygon verts = { {-29.8f,-55.2f}, {32.8f,-54.6f}, {63.6f,-0.2f}, {31.8f,53.7f}, {-30.7f,53.2f}, {-61.5f,-1.3f} };
	asteroidLarge->AddComponent<PolygonCollisionComponent>(asteroidLarge, verts, Asteroid::DefaultCollisionTagsSelf, Asteroid::DefaultCollisionTagsOther);
	
	m_PoolManager->CreatePool(Asteroid::AsteroidLargePoolName, asteroidLarge, 5, 10, .5f, 1.f);

	// medium asteroid prefab
	std::shared_ptr<GameObject> asteroidMedium = std::make_shared<Asteroid>(ObjectTextures[astMediumTexIndex], AST_SIZE::medium);
	asteroidMedium->AddComponent<PooledObjectComponent>(asteroidMedium, nullptr);
	asteroidMedium->AddComponent<CircleCollisionComponent>(asteroidMedium, 40.f, Asteroid::DefaultCollisionTagsSelf, Asteroid::DefaultCollisionTagsOther);
	
	m_PoolManager->CreatePool(Asteroid::AsteroidMediumPoolName, asteroidMedium, 5, 10, .5f, 1.f);

	// small asteroid prefab
	std::shared_ptr<GameObject> asteroidSmall = std::make_shared<Asteroid>(ObjectTextures[astSmallTexIndex], AST_SIZE::small);
	asteroidSmall->AddComponent<PooledObjectComponent>(asteroidSmall, nullptr);
	asteroidSmall->AddComponent<CircleCollisionComponent>(asteroidSmall, 25.f, Asteroid::DefaultCollisionTagsSelf, Asteroid::DefaultCollisionTagsOther);
	
	m_PoolManager->CreatePool(Asteroid::AsteroidSmallPoolName, asteroidSmall, 5, 10, .5f, 1.f);
}