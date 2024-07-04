#pragma once
#include "Top.h"
#include "ThreadSafeSet.h"
#include <unordered_map>

class Component;

// base class from which all objects should inherit, acts as a wrapper of sorts for sf::Sprite and also owns the components e.g. collision for that object
// the position and rotation in the sprite is used to store a snapshot of the object's state at the end of the previous frame, the main thread
// can then draw using that, whilst the other threads are updating each object's m_Rotation and m_Position
class GameObject
{
private:
	static std::atomic<int> NextId;
	int m_ID;
	std::unordered_map<int, std::unique_ptr<Component>> m_Components;

protected:
	sf::Sprite m_Sprite;
	bool m_Active = true;
	float m_Rotation = 0;
	sf::Vector2f m_Position = { 0,0 };

public:
	GameObject();
	GameObject(const GameObject&);
	virtual ~GameObject() {}

	// gets/sets
	void SetActive() { m_Active = true;	}
	void SetInactive() { m_Active = false; }
	bool GetActive() const { return m_Active; }
	int getId() const {	return m_ID; }
	float GetRotation() const { return m_Rotation; }
	sf::Vector2f GetPosition() const { return m_Position; }
	virtual bool GetOccluder() const { return false; }
	void SetBothRotations(float rot);
	void SetBothPositions(const sf::Vector2f& pos);

	// used for double buffering
	void CreateSnapshot();
	// called every frame in parallel with all other updates
	virtual void Update(float deltaTime) = 0;

	// called when object is returned from an object pool
	void ReinitialiseObject(const sf::Vector2f& newPos, const float& newRot, const std::shared_ptr<GameObject>& obj);
	// called in above function, implements class specific reinitialisation
	virtual void Reinitialise() = 0;

	// sprite
	void SetSpriteTexture(sf::Texture& _tex);
	sf::Sprite& GetSprite() { return m_Sprite; }

	// resolve collision between this and another object, first checks intersection, then if both active, obtains mutexes from each object's 
	// collision component and calls HandleCollision if successful
	bool CollisionWith(GameObject* other, uint16_t selfTags, uint16_t otherTags, thread_safe_set<std::pair<int, int>>& completedCollisions, const std::pair<int,int>& pair);
	// no need to store the completed collision as one or both of the pair take up only one cell in the collision grid so no risk of double count
	bool CollisionWithUnique(GameObject* other, uint16_t selfTags, uint16_t otherTags);
	// what should happen after a succesful collision is detected, can depend on the tags of the other object
	virtual void HandleCollision(uint16_t otherTags) = 0;

	// texture coords from texture atlas (not every tex is from an atlas)
	virtual sf::Vector2f GetTextureAtlasOffsetTL() const { return { 0,0 }; }
	virtual sf::Vector2f GetTextureAtlasOffsetBR() const { return { 0,0 }; }

	// clone function used in object pool
	virtual std::shared_ptr<GameObject> CloneToSharedPtr() = 0;
	void CloneComponentsFromOther(std::shared_ptr<GameObject>& self, GameObject* other);

	// access and add components by type, no RTTI, uses class specific int to key the map, allows forwarding of args to component constructor
	template<typename T, typename... Args>
	T* AddComponent(Args&&... args);
	template<typename T>
	T* GetComponent();

};

template<typename T, typename... Args>
T* GameObject::AddComponent(Args&&... args) 
{
	static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
	auto uComponent = std::make_unique<T>(std::forward<Args>(args)...);
	auto pComponent = uComponent.get();
	m_Components[T::GetId()] = std::move(uComponent);
	return pComponent;
}

template<typename T>
T* GameObject::GetComponent()
{
	static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
	return static_cast<T*>(m_Components[T::GetId()].get());
}