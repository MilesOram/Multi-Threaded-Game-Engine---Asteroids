#include "ObjectPool.h"
#include "GameObject.h"
#include "Components.h"
#include "Gamestate.h"
#include "GamestateTemplates.h"

std::atomic<int> GameObject::NextId{ 0 };

GameObject::GameObject()
{
	m_ID = NextId.fetch_add(1);
}
GameObject::GameObject(const GameObject& other)
{
	m_Active = other.m_Active;
	m_Sprite = other.m_Sprite;
	m_Position = other.m_Position;
	m_Rotation = other.m_Rotation;
	m_ID = NextId.fetch_add(1);
}
void GameObject::CreateSnapshot()
{
	m_Sprite.setRotation(m_Rotation);
	m_Sprite.setPosition(m_Position);
}
void GameObject::SetBothRotations(float rot)
{
	m_Rotation = rot;
	m_Sprite.setRotation(rot);
}
void GameObject::SetBothPositions(const sf::Vector2f& pos)
{
	m_Position = pos;
	m_Sprite.setPosition(pos);
}
void GameObject::SetSpriteTexture(sf::Texture& _tex)
{
	m_Sprite.setTexture(_tex);
	m_Sprite.setOrigin((sf::Vector2f)_tex.getSize() / 2.f);
}


void GameObject::ReinitialiseObject(const sf::Vector2f& newPos, const float& newRot, const std::shared_ptr<GameObject>& obj)
{
	SetBothPositions(newPos);
	SetBothRotations(newRot);
	Gamestate::instance->AddToActiveObjects(obj);
	Reinitialise();
}

void GameObject::CloneComponentsFromOther(std::shared_ptr<GameObject>& self, GameObject* other)
{
	for (auto itr = other->m_Components.begin(); itr != other->m_Components.end(); ++itr)
	{
		self->m_Components[itr->first] = itr->second->CloneToUniquePtr(self);
	}
}


bool GameObject::CollisionWith(GameObject* other, uint16_t selfTags, uint16_t otherTags, thread_safe_set<std::pair<int, int>>& completedCollisions, const std::pair<int, int>& pair)
{
	// check intersection first, at this point the tags are valid, but the intersection is unknown
	if (!GetComponent<CollisionComponent>()->CheckCollisionWith(other->GetComponent<CollisionComponent>(), otherTags))
	{
		return false;
	}
	// obtain mutexes for both objects to avoid double counting collisions, to avoid deadlock, obtain the locks in order of id
	int thisId = getId();
	int otherId = other->getId();
	bool thisObjectFirst = thisId < otherId;

	std::mutex& firstCollMutex = thisObjectFirst ? GetComponent<CollisionComponent>()->GetMutex() : other->GetComponent<CollisionComponent>()->GetMutex();
	std::mutex& secondCollMutex = thisObjectFirst ? other->GetComponent<CollisionComponent>()->GetMutex() : GetComponent<CollisionComponent>()->GetMutex();

	std::lock_guard<std::mutex> firstLock(firstCollMutex);
	if (!GetActive() || !other->GetActive() || completedCollisions.find(pair) != completedCollisions.end())
	{
		return false;
	}

	std::lock_guard<std::mutex> secondLock(secondCollMutex);
	if (!GetActive() || !other->GetActive() || completedCollisions.find(pair) != completedCollisions.end())
	{
		return false;
	}
	// store this pair as a completed collision, so any other threads will stop checking earlier
	completedCollisions.insert(pair);
	// class specific collision handling, currently have no need for args from the other object outside of the tags, but can very easily
	// give these as well if needed
	HandleCollision(otherTags);
	other->HandleCollision(selfTags);

	return true;
}
bool GameObject::CollisionWithUnique(GameObject* other, uint16_t selfTags, uint16_t otherTags)
{
	// similar to above, but at least one of the objects only takes up one cell in the grid
	if (!GetComponent<CollisionComponent>()->CheckCollisionWith(other->GetComponent<CollisionComponent>(), otherTags))
	{
		return false;
	}

	int thisId = getId();
	int otherId = other->getId();
	bool thisObjectFirst = thisId < otherId;

	std::mutex& firstCollMutex = thisObjectFirst ? GetComponent<CollisionComponent>()->GetMutex() : other->GetComponent<CollisionComponent>()->GetMutex();
	std::mutex& secondCollMutex = thisObjectFirst ? other->GetComponent<CollisionComponent>()->GetMutex() : GetComponent<CollisionComponent>()->GetMutex();

	std::lock_guard<std::mutex> firstLock(firstCollMutex);
	if (!GetActive() || !other->GetActive())
	{
		return false;
	}
	std::lock_guard<std::mutex> secondLock(secondCollMutex);
	if (!GetActive() || !other->GetActive())
	{
		return false;
	}

	HandleCollision(otherTags);
	other->HandleCollision(selfTags);

	return true;
}



