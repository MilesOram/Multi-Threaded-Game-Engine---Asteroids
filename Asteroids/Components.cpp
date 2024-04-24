#include "ObjectPool.h"
#include "GameObject.h"
#include "Components.h"
#include "Gamestate.h"

#include <immintrin.h>
#include <cmath>
#include <algorithm>

const float CollisionComponent::CELL_SIZE_X = SCREEN_WIDTH / GRID_RESOLUTION;
const float CollisionComponent::CELL_SIZE_Y = SCREEN_HEIGHT / GRID_RESOLUTION;
ComponentIdCounter Component::IdCounter{};

std::shared_ptr<GameObject> Component::GetParentSharedPtr()
{
	return m_pWeakParent.lock();
}
sf::Vector2f CollisionComponent::GetPos()
{
	return m_ParentObject->GetPosition();
}
float CollisionComponent::GetRot()
{
	return m_ParentObject->GetRotation();
}

std::unique_ptr<Component> CircleCollisionComponent::CloneToUniquePtr(std::shared_ptr<GameObject>& parent)
{
	return std::make_unique<CircleCollisionComponent>(*this, parent);
}
std::unique_ptr<Component> BoxCollisionComponent::CloneToUniquePtr(std::shared_ptr<GameObject>& parent)
{
	return std::make_unique<BoxCollisionComponent>(*this, parent);
}
std::unique_ptr<Component> PolygonCollisionComponent::CloneToUniquePtr(std::shared_ptr<GameObject>& parent)
{
	return std::make_unique<PolygonCollisionComponent>(*this, parent);
}
std::unique_ptr<Component> PooledObjectComponent::CloneToUniquePtr(std::shared_ptr<GameObject>& parent)
{
	return std::make_unique<PooledObjectComponent>(*this, parent);
}

Component::Component(const Component& other, std::shared_ptr<GameObject>& parent)
{
	m_ParentObject = parent.get();
	m_pWeakParent = parent;
}
CollisionComponent::CollisionComponent(const CollisionComponent& other, std::shared_ptr<GameObject>& parent) : Component(other, parent)
{
	m_CollisionTagsSelf = other.m_CollisionTagsSelf;
	m_CollisionTagsOther = other.m_CollisionTagsOther;
}
CircleCollisionComponent::CircleCollisionComponent(const CircleCollisionComponent& other, std::shared_ptr<GameObject>& parent) : CollisionComponent(other, parent)
{
	m_Radius = other.m_Radius;
}
BoxCollisionComponent::BoxCollisionComponent(const BoxCollisionComponent& other, std::shared_ptr<GameObject>& parent) : CollisionComponent(other, parent)
{
	m_HalfWidth = other.m_HalfWidth;
	m_HalfHeight = other.m_HalfHeight;
}
PolygonCollisionComponent::PolygonCollisionComponent(const PolygonCollisionComponent& other, std::shared_ptr<GameObject>& parent) : CollisionComponent(other, parent)
{
	m_Vertices = other.m_Vertices;
}
PooledObjectComponent::PooledObjectComponent(const PooledObjectComponent& other, std::shared_ptr<GameObject>& parent) : Component(other, parent)
{
	m_pObjectPool = other.m_pObjectPool;
}

void PooledObjectComponent::ReturnToPool(std::shared_ptr<GameObject> self)
{
	m_pObjectPool->AddToPool(self);
}

PhaseBox CircleCollisionComponent::GetBroadPhaseBox(bool& NoChange) 
{
	auto centre = m_ParentObject->GetPosition();
	PhaseBox NewPhaseBox{
		std::clamp(static_cast<int>((centre.x - m_Radius) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
		std::clamp(static_cast<int>((centre.x + m_Radius) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
		std::clamp(static_cast<int>((centre.y - m_Radius) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1),
		std::clamp(static_cast<int>((centre.y + m_Radius) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1)
	};
	NoChange = NewPhaseBox == m_PreviousPhaseBox;
	return NewPhaseBox;
}
// works poorly for a rotated long and thin box, otherwise is a reasonable approximation for insertion into collision grid
PhaseBox BoxCollisionComponent::GetBroadPhaseBox(bool& NoChange)
{
	float rotation = m_ParentObject->GetRotation() * TO_RADIANS;
	sf::Vector2f centre = m_ParentObject->GetPosition();

	if (rotation == 0)
	{
		PhaseBox NewPhaseBox{
			std::clamp(static_cast<int>((centre.x - m_HalfWidth) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
			std::clamp(static_cast<int>((centre.x + m_HalfWidth) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
			std::clamp(static_cast<int>((centre.y - m_HalfHeight) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1),
			std::clamp(static_cast<int>((centre.y + m_HalfHeight) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1)
		};

		NoChange = NewPhaseBox == m_PreviousPhaseBox;
		return NewPhaseBox;
	}
	else
	{
		float corners[] = {  - m_HalfWidth, - m_HalfWidth, m_HalfWidth, m_HalfWidth,
							 - m_HalfHeight, m_HalfHeight, - m_HalfHeight, m_HalfHeight };
		RotateBox(corners, std::sin(rotation), std::cos(rotation));
		float minX = corners[0];
		float maxX = minX;
		float minY = corners[4];
		float maxY = minY;
		for (int i=1; i<4; ++i)
		{
			if (corners[i] > maxX) maxX = corners[i];
			else if (corners[i] < minX) minX = corners[i];
			if (corners[i+4] > maxY) maxY = corners[i+4];
			else if (corners[i+4] < minY) minY = corners[i+4];
		}
		PhaseBox NewPhaseBox{
			std::clamp(static_cast<int>((centre.x + minX) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
			std::clamp(static_cast<int>((centre.x + maxX) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
			std::clamp(static_cast<int>((centre.y + minY) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1),
			std::clamp(static_cast<int>((centre.y + maxY) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1)
		};
		NoChange = NewPhaseBox == m_PreviousPhaseBox;
		return NewPhaseBox;
	}
}
PhaseBox PolygonCollisionComponent::GetBroadPhaseBox(bool& NoChange)
{
	sf::Vector2f centre = m_ParentObject->GetPosition();
	Polygon polygon = GetPolygon();
	float minX = polygon[0].x;
	float maxX = minX;
	float minY = polygon[0].y;
	float maxY = minY;
	for (auto& vert : polygon)
	{
		if (vert.x > maxX) maxX = vert.x;
		else if (vert.x < minX) minX = vert.x;
		if (vert.y > maxY) maxY = vert.y;
		else if (vert.y < minY) minY = vert.y;
	}
	PhaseBox NewPhaseBox{
		std::clamp(static_cast<int>((minX) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
		std::clamp(static_cast<int>((maxX) / CELL_SIZE_X), 0, GRID_RESOLUTION-1),
		std::clamp(static_cast<int>((minY) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1),
		std::clamp(static_cast<int>((maxY) / CELL_SIZE_Y), 0, GRID_RESOLUTION-1)
	};

	NoChange = NewPhaseBox == m_PreviousPhaseBox;
	return NewPhaseBox;
}
// use double dispatch to handle collisions of various derived collision components
bool CircleCollisionComponent::CheckCollisionWith(CollisionComponent* other, uint16_t otherTags)
{
	return other->Intersects(this);
}
bool BoxCollisionComponent::CheckCollisionWith(CollisionComponent* other, uint16_t otherTags)
{
	return other->Intersects(this);
}
bool PolygonCollisionComponent::CheckCollisionWith(CollisionComponent* other, uint16_t otherTags)
{
	return other->Intersects(this);
}
// simplest case
bool CircleCollisionComponent::Intersects(CircleCollisionComponent* other)
{
	sf::Vector2f dif = GetPos() - other->GetPos();
	float distSquared = (dif.x * dif.x + dif.y * dif.y);
	return (m_Radius + other->GetRadius()) * (m_Radius + other->GetRadius()) > distSquared;
}
// hand off to the reverse pair intersection to not duplicate the function
bool CircleCollisionComponent::Intersects(BoxCollisionComponent* other)
{
	return other->Intersects(this);
}
bool CircleCollisionComponent::Intersects(PolygonCollisionComponent* other)
{
	return other->Intersects(this);
}
// all non circle-circle cases use 2D GJK algorithm
bool BoxCollisionComponent::Intersects(CircleCollisionComponent* other)
{
	Polygon shape = GetPolygon();
	return GJKCirclePolygon({ other->GetPos().x, other->GetPos().y }, other->GetRadius(), shape);
}
bool BoxCollisionComponent::Intersects(BoxCollisionComponent* other)
{
	Polygon shape1 = GetPolygon();
	Polygon shape2 = other->GetPolygon();
	return GJK(shape1, shape2);
}
bool BoxCollisionComponent::Intersects(PolygonCollisionComponent* other)
{
	return other->Intersects(this);
}
bool PolygonCollisionComponent::Intersects(CircleCollisionComponent* other)
{
	Polygon shape = GetPolygon();
	return GJKCirclePolygon({ other->GetPos().x, other->GetPos().y }, other->GetRadius(), shape);
}
bool PolygonCollisionComponent::Intersects(BoxCollisionComponent* other)
{
	Polygon shape1 = GetPolygon();
	Polygon shape2 = other->GetPolygon();
	return GJK(shape1, shape2);
}
bool PolygonCollisionComponent::Intersects(PolygonCollisionComponent* other)
{
	Polygon shape1 = GetPolygon();
	Polygon shape2 = other->GetPolygon();
	return GJK(shape1, shape2);
}
// return the corners of the (rotated) box
Polygon BoxCollisionComponent::GetPolygon()
{
	sf::Vector2f centre = m_ParentObject->GetPosition();
	float rotation = GetRot() * TO_RADIANS;
	if (GetRot() == 0)
	{
		return
		{
			{centre.x - m_HalfWidth,centre.y - m_HalfHeight},
			{centre.x - m_HalfWidth,centre.y + m_HalfHeight},
			{centre.x + m_HalfWidth,centre.y - m_HalfHeight},
			{centre.x + m_HalfWidth,centre.y + m_HalfHeight}
		};
	}
	else
	{
		float corners[] = { - m_HalfWidth, - m_HalfWidth,  m_HalfWidth, m_HalfWidth,
							- m_HalfHeight, m_HalfHeight, - m_HalfHeight, m_HalfHeight };
		RotateBox(corners, std::sin(rotation), std::cos(rotation));
		return
		{
			{centre.x + corners[0],centre.y + corners[4]},
			{centre.x + corners[1],centre.y + corners[5]},
			{centre.x + corners[2],centre.y + corners[6]},
			{centre.x + corners[3],centre.y + corners[7]}
		};
	}
}
// return the vertices of the (rotated) polygon
Polygon PolygonCollisionComponent::GetPolygon()
{
	sf::Vector2f centre = m_ParentObject->GetPosition();
	float rotation = GetRot() * TO_RADIANS;
	if (rotation == 0)
	{
		Polygon result = m_Vertices;
		for (auto& v : result)
		{
			v.x += centre.x;
			v.y += centre.y;
		}
		return result;
	}
	else
	{
		Polygon result;
		size_t index = 0;
		float sinAnlge = std::sin(rotation);
		float cosAnlge = std::cos(rotation);
		while (m_Vertices.size() - index >=4)
		{
			float corners[] = { 
				m_Vertices[index].x, 
				m_Vertices[index+1].x, 
				m_Vertices[index+2].x, 
				m_Vertices[index+3].x, 
				m_Vertices[index].y, 
				m_Vertices[index+1].y, 
				m_Vertices[index+2].y, 
				m_Vertices[index+3].y 
			};
			RotateBox(corners, sinAnlge, cosAnlge);
			for (int i = 0; i < 4; ++i)
			{
				result.emplace_back(corners[i]+centre.x, corners[i + 4]+centre.y);
			}
			index += 4;
		}
		while (index < m_Vertices.size())
		{
			result.push_back(RotatePoint(m_Vertices[index++], sinAnlge, cosAnlge) + Vector2D(centre.x, centre.y));
		}
		return result;
	}
}
// prefer rotate box, used for any remaining points
Vector2D CollisionComponent::RotatePoint(const Vector2D& Point, float cosTheta, float sinTheta)
{
    return { Point.x * cosTheta - Point.y * sinTheta,
            Point.x * sinTheta + Point.y * cosTheta };
}
// requires prepping of coords into [x1,x2,x3,x4,y1,y2,y3,y4] from all the {x,y}s, coords must be centred on the origin (not the current object position)
// works for any 4 coordinates, not necessarily a box, can be used for most of polygon rotation
void CollisionComponent::RotateBox(float* coords, float sinAngle, float cosAngle) {
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
	_mm_storeu_ps(coords, x);
	_mm_storeu_ps(coords + 4, y);
}
// called in object's update function, if position has changed sufficiently and the object has a new phase box, update its position in the collision grid
// store offsets for the nodes in each cell
void CollisionComponent::UpdateInCollisionGrid()
{
	bool noChange = !m_bTagsWereUpdated;
	if (m_bTagsWereUpdated) m_bTagsWereUpdated = false;

	auto pos = GetPos();
	auto rot = GetRot();
	if (abs(pos.x - m_LastUpdatedX) < .5f && abs(pos.y - m_LastUpdatedY) < .5f && abs(rot - m_LastUpdatedRot) < .5f && noChange)
	{
		return;
	}
	m_LastUpdatedX = pos.x;
	m_LastUpdatedY = pos.y;
	m_LastUpdatedRot = rot;
	m_CurrentPhaseBox = GetBroadPhaseBox(noChange);

	Polygon p = GetPolygon();
	if (!noChange)
	{
		if (m_CollisionGridNodeIndices.size() > 0)
		{
			int boxSize = m_PreviousPhaseBox.Right - m_PreviousPhaseBox.Left + 1;
			for (int i = m_PreviousPhaseBox.Top; i <= m_PreviousPhaseBox.Bottom; ++i)
			{
				for (int j = m_PreviousPhaseBox.Left; j <= m_PreviousPhaseBox.Right; ++j)
				{
					Gamestate::instance->RemoveFromCollisionGrid(m_CollisionGridNodeIndices[(i - m_PreviousPhaseBox.Top) * boxSize + j - m_PreviousPhaseBox.Left], j, i);
				}
			}
		}
		uint16_t selfTag = m_CollisionTagsSelf;
		// use the last bit to signify whether the object exists in more than one cell in the grid to save on checks for duplicate concurrent collisions
		if (m_CurrentPhaseBox.Bottom != m_CurrentPhaseBox.Top || m_CurrentPhaseBox.Right != m_CurrentPhaseBox.Left)
		{
			selfTag |= 0b01;
		}
		m_NewGridIndices.clear();
		for (int i = m_CurrentPhaseBox.Top; i <= m_CurrentPhaseBox.Bottom; ++i)
		{
			for (int j = m_CurrentPhaseBox.Left; j <= m_CurrentPhaseBox.Right; ++j)
			{
				m_NewGridIndices.push_back(Gamestate::instance->AddToCollisionGrid(m_ParentObject, j, i, selfTag, m_CollisionTagsOther));
			}
		}
		m_PreviousPhaseBox = m_CurrentPhaseBox;
		m_CollisionGridNodeIndices = m_NewGridIndices;
	}
}

void CollisionComponent::ClearFromGrid()
{
	int boxSize = m_PreviousPhaseBox.Right - m_PreviousPhaseBox.Left + 1;
	for (int i = m_PreviousPhaseBox.Top; i <= m_PreviousPhaseBox.Bottom; ++i)
	{
		for (int j = m_PreviousPhaseBox.Left; j <= m_PreviousPhaseBox.Right; ++j)
		{
			Gamestate::instance->RemoveFromCollisionGrid(m_CollisionGridNodeIndices[(i - m_PreviousPhaseBox.Top)*boxSize + j - m_PreviousPhaseBox.Left], j, i);
		}
	}
	m_CollisionGridNodeIndices.clear();
	m_bTagsWereUpdated = true;
}
// support for GJK of circle and polygon
Vector2D CollisionComponent::Support(Vector2D circleCentre, float radius, const Polygon& polygon, const Vector2D& direction) 
{
	Vector2D maxPointPolygon = *std::max_element(polygon.begin(), polygon.end(), [&](const Vector2D& a, const Vector2D& b) 
	{
		return a.dot(direction) < b.dot(direction);
	});

	Vector2D maxPointCircle = circleCentre - direction * (radius / direction.length());

	return maxPointPolygon - maxPointCircle;
}
// support for GJK of polygon and polygon
Vector2D CollisionComponent::Support(const Polygon& shape1, const Polygon& shape2, const Vector2D& direction) {
	Vector2D maxPoint1 = *std::max_element(shape1.begin(), shape1.end(), [&](const Vector2D& a, const Vector2D& b) {
		return a.dot(direction) < b.dot(direction);
		});

	Vector2D maxPoint2 = *std::max_element(shape2.begin(), shape2.end(), [&](const Vector2D& a, const Vector2D& b) {
		return a.dot(-direction) < b.dot(-direction);
		});

	return maxPoint1 - maxPoint2;
}

bool CollisionComponent::GJK(const Polygon& shape1, const Polygon& shape2) {
	Vector2D simplex[3];
	simplex[0] = Support(shape1, shape2, Vector2D(1, 1));

	Vector2D direction = -simplex[0];

	int simplexSize = 1;
	int orientation = 1;
	while (true)
	{
		Vector2D newPoint = Support(shape1, shape2, direction);

		// new point is not past the origin, impossible for intersection of convex shapes
		if (newPoint.dot(direction) <= 0)
		{
			return false;
		}

		simplex[simplexSize++] = newPoint;

		if (simplexSize == 3)
		{
			Vector2D ao = -simplex[2];
			Vector2D ab = simplex[1] - simplex[2];
			Vector2D ac = simplex[0] - simplex[2];

			Vector2D abPerp = ab.perp(orientation);

			// not within the current triagnle and outside of the ab edge, get rid of c and find another point to make a new triangle, heading towards the origin
			if (abPerp.dot(ao) > 0)
			{
				simplex[0] = simplex[1];
				simplex[1] = simplex[2];
				simplexSize = 2;
				direction = abPerp;
				orientation = -orientation;
			}
			// same case but for the ac edge, get rid of b and keep going
			else
			{
				Vector2D acPerp = ac.perp(-orientation);
				if (acPerp.dot(ao) > 0)
				{
					simplex[1] = simplex[2];
					simplexSize = 2;
					direction = acPerp;
				}
				// we know point a is past the origin, and it's not outside the ab or ac edges, so it must be inside the triangle
				else 
				{
					return true;
				}
			}
		}
		else 
		{
			// calculate the direction for the thrid point, must be towrads the origin
			Vector2D ab = simplex[1] - simplex[0];
			Vector2D ao = -simplex[0];
			Vector2D newDirection = ab.perp();
			if (newDirection.dot(ao) > 0)
			{
				orientation = 1;
				direction = newDirection;
			}
			else
			{
				orientation = -1;
				direction = -newDirection;
			}
		}
	}
}
bool CollisionComponent::GJKCirclePolygon(Vector2D circleCentre, float radius, const Polygon& polygon)
{
	Vector2D simplex[3];
	simplex[0] = Support(circleCentre, radius, polygon, Vector2D(1, 0));

	Vector2D direction = -simplex[0];

	int simplexSize = 1;
	int orientation = 1;
	while (true) 
	{
		Vector2D newPoint = Support(circleCentre, radius, polygon, direction);

		// new point is not past the origin, impossible for intersection of convex shapes
		if (newPoint.dot(direction) <= 0)
		{
			return false;
		}

		simplex[simplexSize++] = newPoint;

		if (simplexSize == 3)
		{
			Vector2D ao = -simplex[2];
			Vector2D ab = simplex[1] - simplex[2];
			Vector2D ac = simplex[0] - simplex[2];

			Vector2D abPerp = ab.perp(orientation);

			// not within the current triagnle and outside of the ab edge, get rid of c and find another point to make a new triangle, heading towards the origin
			if (abPerp.dot(ao) > 0)
			{
				simplex[0] = simplex[1];
				simplex[1] = simplex[2];
				simplexSize = 2;
				direction = abPerp;
				orientation = -orientation;
			}
			// same case but for the ac edge, get rid of b and keep going
			else
			{
				Vector2D acPerp = ac.perp(-orientation);
				if (acPerp.dot(ao) > 0)
				{
					simplex[1] = simplex[2];
					simplexSize = 2;
					direction = acPerp;
				}
				// we know point a is past the origin, and it's not outside the ab or ac edges, so it must be inside the triangle
				else
				{
					return true;
				}
			}
		}
		else
		{
			// calculate the direction for the thrid point, must be towrads the origin
			Vector2D ab = simplex[1] - simplex[0];
			Vector2D ao = -simplex[0];
			Vector2D newDirection = ab.perp();
			if (newDirection.dot(ao) > 0)
			{
				orientation = 1;
				direction = newDirection;
			}
			else
			{
				orientation = -1;
				direction = -newDirection;
			}
		}
	}
}

