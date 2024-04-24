#pragma once
#include "Top.h"
#include <mutex>

class GameObject;
class ObjectPoolBase;
class ObjectPool;
struct Node;

// x,y coords for vertices of polygons
struct Vector2D {
	float x, y;

	Vector2D(float x = 0, float y = 0) : x(x), y(y) {}

	Vector2D operator-(const Vector2D& other) const {
		return Vector2D(x - other.x, y - other.y);
	}
	Vector2D operator+(const Vector2D& other) const {
		return Vector2D(x + other.x, y + other.y);
	}
	Vector2D operator-() const {
		return Vector2D(-x, -y);
	}
	Vector2D operator*(const float& mult) const {
		return Vector2D(x*mult, y*mult);
	}
	float dot(const Vector2D& other) const {
		return x * other.x + y * other.y;
	}
	Vector2D perp() const {
		return Vector2D(-y, x);
	}
	// orientation is either -1 or 1
	Vector2D perp(int orientation) const {
		return Vector2D(-y * orientation, x * orientation);
	}	
	float lengthSquared() const {
		return x * x + y * y;
	}
	float length() const {
		return std::sqrtf(x * x + y * y);
	}
};
typedef std::vector<Vector2D> Polygon;

// allows components to be keyed by an int (unique to the type T) and avoid RTTI
class ComponentIdCounter {
public:
	int counter = 0;
	template<typename T>
	int getId() {
		static int staticID = counter++;
		return staticID;
	}
};

// extensible component system, stored in a map in each GameObject
class Component
{
private:
	static ComponentIdCounter IdCounter;
protected:
	// parent - store raw pointer for quicker access, and weak to give a sharedptr to the object when needed (avoids passing sharedptr everywhere)
	std::weak_ptr<GameObject> m_pWeakParent;
	GameObject* m_ParentObject;
	template<typename T>
	static int GetIdOfComponent() { return IdCounter.getId<T>(); }
public:
	// construct with parent only (either default or copy constructor)
	Component(std::shared_ptr<GameObject>& parent) : m_pWeakParent(parent), m_ParentObject(parent.get()) {}
	Component(const Component& other, std::shared_ptr<GameObject>& parent);
	std::shared_ptr<GameObject> GetParentSharedPtr();
	// clone used when copying a game object
	virtual std::unique_ptr<Component> CloneToUniquePtr(std::shared_ptr<GameObject>& parent) = 0;
	virtual ~Component() {}
};
// for objects which come from a pool and should be returned there - assumes pool's lifetime will always outlast its own
class PooledObjectComponent : public Component
{
private:
	// will return to this pool on inactive
	ObjectPool* m_pObjectPool;
public:
	PooledObjectComponent(const PooledObjectComponent& other, std::shared_ptr<GameObject>& parent);
	PooledObjectComponent(std::shared_ptr<GameObject>& parent, ObjectPool* pool) : m_pObjectPool(pool), Component(parent) {}
	void ReturnToPool(std::shared_ptr<GameObject> self);
	void SetPool(ObjectPool* pool) { m_pObjectPool = pool; }
	std::unique_ptr<Component> CloneToUniquePtr(std::shared_ptr<GameObject>& parent) override;
	static int GetId() { return GetIdOfComponent<PooledObjectComponent>(); }
};

class CircleCollisionComponent;
class BoxCollisionComponent;
class PolygonCollisionComponent;

class CollisionComponent : public Component
{
protected:
	static const float CELL_SIZE_X;
	static const float CELL_SIZE_Y;
	float m_LastUpdatedX=0;
	float m_LastUpdatedY=0;
	float m_LastUpdatedRot=0;
	// see GamestateTemplates.h for info on tags
	uint16_t m_CollisionTagsSelf = 0;
	uint16_t m_CollisionTagsOther = 0;
	// phase box - extremeties for top,bottom,left,right coords, used for placement in collision grid
	PhaseBox m_PreviousPhaseBox;
	PhaseBox m_CurrentPhaseBox;
	// stores the node offsets for where this is stored in the collision grid - allows faster removal
	std::vector<uint8_t> m_CollisionGridNodeIndices;
	std::vector<uint8_t> m_NewGridIndices;
	// lock mutex to stop double counting collisions
	std::mutex m_CollisionMutex;
	// usually only update collision grid when phase box changes, updated tags is equally a valid reason to update, since tags are stored in the nodes
	bool m_bTagsWereUpdated = false;
	// helper functions to resolve intersections - uses GJK for all except circle-circle (RotateBox is vectorized)
	static void RotateBox(float* coords, float sinAngle, float cosAngle);
	static Vector2D RotatePoint(const Vector2D& Point, float cosTheta, float sinTheta);
	static Vector2D Support(const Polygon& shape1, const Polygon& shape2, const Vector2D& direction);
	static Vector2D Support(Vector2D circleCentre, float radius, const Polygon& polygon, const Vector2D& direction); 
	static bool GJK(const Polygon& shape1, const Polygon& shape2);
	static bool GJKCirclePolygon(Vector2D circleCentre, float radius, const Polygon& polygon);
public:
	CollisionComponent(const CollisionComponent& other, std::shared_ptr<GameObject>& parent);
	CollisionComponent(std::shared_ptr<GameObject>& parent, uint16_t selfTag, uint16_t otherTag): Component(parent), m_CollisionTagsSelf(selfTag), m_CollisionTagsOther(otherTag) {}
	// gets/sets
	auto GetTags() const { return std::pair<uint16_t, uint16_t>(m_CollisionTagsSelf, m_CollisionTagsOther); }
	auto GetSelfTag() const { return m_CollisionTagsSelf; }
	void SetSelfTag(uint16_t tag) { m_CollisionTagsSelf = tag; }
	std::mutex& GetMutex() { return m_CollisionMutex; }
	sf::Vector2f GetPos();
	float GetRot();
	// called every update and updates grid if either moved/rotated to new phase box or tags changed
	void UpdateInCollisionGrid();
	// called during cleanup
	void ClearFromGrid();
	virtual PhaseBox GetBroadPhaseBox(bool& NoChange) = 0;

	// to add a new class derived from CollisionComponent, every pair intersects function will need to be implemented in the derived class
	// must override checkCollisionsWith and give 'this' to other collision component to call the correct pair intersects function
	virtual bool CheckCollisionWith(CollisionComponent* other, uint16_t otherTags) = 0;
	virtual bool Intersects(CircleCollisionComponent* other) = 0;
	virtual bool Intersects(BoxCollisionComponent* other) = 0;
	virtual bool Intersects(PolygonCollisionComponent* other) = 0;
	// returns a polygon to define the shape
	virtual Polygon GetPolygon() = 0;
	static int GetId() { return GetIdOfComponent<CollisionComponent>(); }
};
class CircleCollisionComponent : public CollisionComponent
{
private:
	float m_Radius = 1;
public:
	CircleCollisionComponent(std::shared_ptr<GameObject>& parent, float rad, uint16_t selfTag, uint16_t otherTag): CollisionComponent(parent, selfTag, otherTag), m_Radius(rad) {}
	CircleCollisionComponent(const CircleCollisionComponent& other, std::shared_ptr<GameObject>& parent);
	CircleCollisionComponent operator=(const CircleCollisionComponent&);
	// overrides
	bool CheckCollisionWith(CollisionComponent* other, uint16_t otherTags) override;
	bool Intersects(CircleCollisionComponent* other) override;
	bool Intersects(BoxCollisionComponent* other) override;
	bool Intersects(PolygonCollisionComponent* other) override;
	PhaseBox GetBroadPhaseBox(bool& NoChange)  override;
	Polygon GetPolygon() override { return Polygon(); }
	std::unique_ptr<Component> CloneToUniquePtr(std::shared_ptr<GameObject>& parent) override;
	float GetRadius() const { return m_Radius; }
};
class BoxCollisionComponent : public CollisionComponent
{
private:
	float m_HalfWidth = 1;
	float m_HalfHeight = 1;
public:
	BoxCollisionComponent(std::shared_ptr<GameObject>& parent, float halfWidth, float halfHeight, uint16_t selfTag, uint16_t otherTag) : CollisionComponent(parent, selfTag, otherTag), m_HalfWidth(halfWidth), m_HalfHeight(halfHeight) {}
	BoxCollisionComponent(const BoxCollisionComponent& other, std::shared_ptr<GameObject>& parent);
	BoxCollisionComponent operator=(const BoxCollisionComponent&);
	// overrides
	bool CheckCollisionWith(CollisionComponent* other, uint16_t otherTags) override;
	bool Intersects(CircleCollisionComponent* other) override;
	bool Intersects(BoxCollisionComponent* other) override;
	bool Intersects(PolygonCollisionComponent* other) override;
	PhaseBox GetBroadPhaseBox(bool& NoChange) override;
	Polygon GetPolygon() override;
	std::unique_ptr<Component> CloneToUniquePtr(std::shared_ptr<GameObject>& parent) override;
};

class PolygonCollisionComponent : public CollisionComponent
{
private:
	Polygon m_Vertices;
	
public:
	PolygonCollisionComponent(std::shared_ptr<GameObject>& parent, Polygon vertices, uint16_t selfTag, uint16_t otherTag) : CollisionComponent(parent, selfTag, otherTag), m_Vertices(vertices){}
	PolygonCollisionComponent(const PolygonCollisionComponent& other, std::shared_ptr<GameObject>& parent);
	PolygonCollisionComponent operator=(const PolygonCollisionComponent&);
	// overrides
	bool CheckCollisionWith(CollisionComponent* other, uint16_t otherTags) override;
	bool Intersects(CircleCollisionComponent* other) override;
	bool Intersects(BoxCollisionComponent* other) override;
	bool Intersects(PolygonCollisionComponent* other) override;
	PhaseBox GetBroadPhaseBox(bool& NoChange) override;
	Polygon GetPolygon() override;
	std::unique_ptr<Component> CloneToUniquePtr(std::shared_ptr<GameObject>& parent) override;
};