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
	m_CachedPolygon.resize(m_Vertices.size());
}
PooledObjectComponent::PooledObjectComponent(const PooledObjectComponent& other, std::shared_ptr<GameObject>& parent) : Component(other, parent)
{
	m_pObjectPool = other.m_pObjectPool;
}

void PooledObjectComponent::ReturnToPool(std::shared_ptr<GameObject> self)
{
	m_pObjectPool->AddToPool(self);
}

void CircleCollisionComponent::MakeBroadPhaseBox(bool& NoChange)
{
	auto centre = m_ParentObject->GetPosition();

	m_CurrentPhaseBox.Left =	std::clamp(static_cast<int>((centre.x - m_Radius) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
	m_CurrentPhaseBox.Right =	std::clamp(static_cast<int>((centre.x + m_Radius) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
	m_CurrentPhaseBox.Top =		std::clamp(static_cast<int>((centre.y - m_Radius) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);
	m_CurrentPhaseBox.Bottom =	std::clamp(static_cast<int>((centre.y + m_Radius) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);

	NoChange = m_CurrentPhaseBox == m_PreviousPhaseBox;
}

// works poorly for a rotated long and thin box, otherwise is a reasonable approximation for insertion into collision grid
void BoxCollisionComponent::MakeBroadPhaseBox(bool& NoChange)
{
	float rotation = m_ParentObject->GetRotation() * TO_RADIANS;
	sf::Vector2f centre = m_ParentObject->GetPosition();

	if (rotation == 0)
	{
		m_CurrentPhaseBox.Left =	std::clamp(static_cast<int>((centre.x - m_HalfWidth) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
		m_CurrentPhaseBox.Right =	std::clamp(static_cast<int>((centre.x + m_HalfWidth) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
		m_CurrentPhaseBox.Top =		std::clamp(static_cast<int>((centre.y - m_HalfHeight) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);
		m_CurrentPhaseBox.Bottom =	std::clamp(static_cast<int>((centre.y + m_HalfHeight) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);

		NoChange = m_CurrentPhaseBox == m_PreviousPhaseBox;
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

		m_CurrentPhaseBox.Left =	std::clamp(static_cast<int>((centre.x + minX) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
		m_CurrentPhaseBox.Right =	std::clamp(static_cast<int>((centre.x + maxX) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
		m_CurrentPhaseBox.Top =		std::clamp(static_cast<int>((centre.y + minY) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);
		m_CurrentPhaseBox.Bottom =	std::clamp(static_cast<int>((centre.y + maxY) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);

		NoChange = m_CurrentPhaseBox == m_PreviousPhaseBox;
	}
}
void PolygonCollisionComponent::MakeBroadPhaseBox(bool& NoChange)
{
	sf::Vector2f centre = m_ParentObject->GetPosition();
	GetPolygon();

	float minX = m_CachedPolygon[0].x;
	float maxX = minX;
	float minY = m_CachedPolygon[0].y;
	float maxY = minY;

	for (auto& vert : m_CachedPolygon)
	{
		if (vert.x > maxX) maxX = vert.x;
		else if (vert.x < minX) minX = vert.x;
		if (vert.y > maxY) maxY = vert.y;
		else if (vert.y < minY) minY = vert.y;
	}

	m_CurrentPhaseBox.Left =	std::clamp(static_cast<int>((minX) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
	m_CurrentPhaseBox.Right =	std::clamp(static_cast<int>((maxX) / CELL_SIZE_X), 0, GRID_RESOLUTION - 1);
	m_CurrentPhaseBox.Top =		std::clamp(static_cast<int>((minY) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);
	m_CurrentPhaseBox.Bottom =	std::clamp(static_cast<int>((maxY) / CELL_SIZE_Y), 0, GRID_RESOLUTION - 1);

	NoChange = m_CurrentPhaseBox == m_PreviousPhaseBox;
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
const Polygon& BoxCollisionComponent::GetPolygon()
{
	if (m_PolygonCalculated && !m_CachedPolygon.empty())
	{
		return m_CachedPolygon;
	}

	m_PolygonCalculated = true;
	sf::Vector2f centre = m_ParentObject->GetPosition();
	float rotation = GetRot() * TO_RADIANS;

	if (GetRot() == 0)
	{
		return m_CachedPolygon =
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
		return m_CachedPolygon =
		{
			{centre.x + corners[0],centre.y + corners[4]},
			{centre.x + corners[1],centre.y + corners[5]},
			{centre.x + corners[2],centre.y + corners[6]},
			{centre.x + corners[3],centre.y + corners[7]}
		};
	}
}

// return the vertices of the (rotated) polygon
const Polygon& PolygonCollisionComponent::GetPolygon()
{
	if (m_PolygonCalculated)
	{
		return m_CachedPolygon;
	}

	m_PolygonCalculated = true;
	sf::Vector2f centre = m_ParentObject->GetPosition();

	float rotation = GetRot() * TO_RADIANS;
	if (rotation == 0)
	{
		m_CachedPolygon = m_Vertices;
		for (auto& v : m_CachedPolygon)
		{
			v.x += centre.x;
			v.y += centre.y;
		}
	}
	else
	{
		size_t index = 0;
		float sinAngle = std::sin(rotation);
		float cosAngle = std::cos(rotation);

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

			RotateBox(corners, sinAngle, cosAngle);

			for (int i = 0; i < 4; ++i)
			{
				m_CachedPolygon[i].x = corners[i] + centre.x;
				m_CachedPolygon[i].y = corners[i + 4] + centre.y;
			}
			index += 4;
		}
		while (index < m_Vertices.size())
		{
			m_CachedPolygon[index] = RotatePoint(m_Vertices[index], sinAngle, cosAngle) + Vector2D(centre.x, centre.y);
			++index;
		}
	}
#if USE_CPU_FOR_OCCLUDERS
	for (size_t i = 0; i < m_CachedPolygon.size(); i++)
	{
		m_VertRegX[i*8] = m_CachedPolygon[i].x;
		m_VertRegX[i*8+1] = m_CachedPolygon[i].x;
		m_VertRegX[i*8+2] = m_CachedPolygon[i].x;
		m_VertRegX[i*8+3] = m_CachedPolygon[i].x;
		m_VertRegX[i*8+4] = m_CachedPolygon[i].x;
		m_VertRegX[i*8+5] = m_CachedPolygon[i].x;
		m_VertRegX[i*8+6] = m_CachedPolygon[i].x;
		m_VertRegX[i*8+7] = m_CachedPolygon[i].x;
					
		m_VertRegY[i*8] = m_CachedPolygon[i].y;
		m_VertRegY[i*8 + 1] = m_CachedPolygon[i].y;
		m_VertRegY[i*8 + 2] = m_CachedPolygon[i].y;
		m_VertRegY[i*8 + 3] = m_CachedPolygon[i].y;
		m_VertRegY[i*8 + 4] = m_CachedPolygon[i].y;
		m_VertRegY[i*8 + 5] = m_CachedPolygon[i].y;
		m_VertRegY[i*8 + 6] = m_CachedPolygon[i].y;
		m_VertRegY[i*8 + 7] = m_CachedPolygon[i].y;
	}
#endif
	return m_CachedPolygon;
}

// prefer rotate box, used for any remaining points
Vector2D CollisionComponent::RotatePoint(const Vector2D& Point, float sinAngle, float cosAngle)
{
    return { Point.x * cosAngle - Point.y * sinAngle,
            Point.x * sinAngle + Point.y * cosAngle };
}

// requires prepping of coords into [x1,x2,x3,x4,y1,y2,y3,y4] from all the {x,y}s, coords must be centred on the origin (not the current object position)
// works for any 4 coordinates, not necessarily a box, can be used for most of polygon rotation
void CollisionComponent::RotateBox(float* coords, float sinAngle, float cosAngle) 
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

// called in object's update function, if position has changed sufficiently and the object has a new phase box, update its position in the collision grid
// store offsets for the nodes in each cell
void CollisionComponent::UpdateInCollisionGrid()
{
	m_PolygonCalculated = false;
	bool noChange = !m_bTagsWereUpdated;
	if (m_bTagsWereUpdated) m_bTagsWereUpdated = false;

	auto pos = GetPos();
	auto rot = GetRot();
	if (abs(pos.x - m_LastUpdatedX) < .1f && abs(pos.y - m_LastUpdatedY) < .1f && abs(rot - m_LastUpdatedRot) < .1f && noChange)
	{
		return;
	}

	m_LastUpdatedX = pos.x;
	m_LastUpdatedY = pos.y;
	m_LastUpdatedRot = rot;
	MakeBroadPhaseBox(noChange);

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

		uint16_t selfTagEdge = selfTag | 0b10;
		m_NewGridIndices.clear();
		for (int i = m_CurrentPhaseBox.Top; i <= m_CurrentPhaseBox.Bottom; ++i)
		{
			for (int j = m_CurrentPhaseBox.Left; j <= m_CurrentPhaseBox.Right; ++j)
			{
				m_NewGridIndices.push_back(Gamestate::instance->AddToCollisionGrid(m_ParentObject, j, i, (i - m_CurrentPhaseBox.Top <= 1 || i - m_CurrentPhaseBox.Bottom >= -1 || j - m_CurrentPhaseBox.Right <= 1 || j - m_CurrentPhaseBox.Left >= -1) ? selfTagEdge : selfTag, m_CollisionTagsOther));
			}
		}
		m_PreviousPhaseBox = m_CurrentPhaseBox;
		m_CollisionGridNodeIndices = m_NewGridIndices;
	}
}

void CollisionComponent::ClearFromGrid()
{
	if (m_CollisionGridNodeIndices.empty()) return;
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
	auto maxElement = [](const Polygon& shape, const Vector2D& dir) -> Vector2D
	{
		int left = 0;
		int right = shape.size() - 1;
		while (right - left > 1)
		{
			int mid = (left + right) / 2;
			if (shape[mid].dot(dir) > shape[left].dot(dir))
			{
				left = mid;
			}
			else
			{
				right = mid;
			}
		}
		return shape[left].dot(dir) > shape[right].dot(dir) ? shape[left] : shape[right];
	};

	Vector2D maxPointPolygon = maxElement(polygon, direction);
	Vector2D maxPointCircle = circleCentre - direction * (radius / direction.length());

	return maxPointPolygon - maxPointCircle;
}

// support for GJK of polygon and polygon
Vector2D CollisionComponent::Support(const Polygon& shape1, const Polygon& shape2, const Vector2D& direction) 
{
	auto maxElement = [](const Polygon& shape, const Vector2D& dir) -> Vector2D 
	{
		int left = 0;
		int right = shape.size();
		while (right - left > 1) 
		{
			int mid = (left + right) / 2;
			if (shape[mid].dot(dir) > shape[left].dot(dir)) 
			{
				left = mid;
			}
			else
			{
				right = mid;
			}
		}
		return shape[left];
	};

	Vector2D maxPoint1 = maxElement(shape1, direction);
	Vector2D maxPoint2 = maxElement(shape2, -direction);

	return maxPoint1 - maxPoint2;
}

bool CollisionComponent::GJK(const Polygon& shape1, const Polygon& shape2)
{
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

#if USE_CPU_FOR_OCCLUDERS
bool PolygonCollisionComponent::CheckPointsInCollider(int* grid, float* xPoints, float* yPoints)
{
	const __m256 zeros = _mm256_setzero_ps();
	const __m256 nans = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
	bool full = true;
	// only set up to work with hexagons currently (or any fixed no.), working with arrays was considerably faster than vectors for loading into __mm256 (maybe they were misaligned?)
	// didn't want 'new' during game loop, templating the poly collision class has knock-on effects etc.
	int verts = 48;
	int sides = 6;
	int sideIndex = 0;
	// unrolled loop to process two rows at a time, going across in sets of 8
	for (int j = 0; j < PATCH_SIZE * 8; j += 16)
	{
		// prefetch the start of the two rows below the current two
		_mm_prefetch((char*)(yPoints + j + 16), _MM_HINT_T1);
		_mm_prefetch((char*)(yPoints + j + 24), _MM_HINT_T1);
		// XY0 = first poly vert, XY1 = second poly vert, P1 = set of points from row 1, P2 = set of points from row 2
		// X coords from P1 and P2 overlap, Y coords are const on a row, yPoints is prepped to include duplicate points to load straight into the 256bit regs
		const __m256 pointY1 = _mm256_loadu_ps(yPoints + j);
		const __m256 pointY2 = _mm256_loadu_ps(yPoints + j + 8);
		for (int i = 0; i < PATCH_SIZE; i += 8)
		{
			const __m256 pointX1 = _mm256_loadu_ps(xPoints + i);

			__m256 mask1 = nans;
			__m256 mask2 = nans;
			for (int count = 0; count < sides; count++)
			{
				// offsets to get correct vertices
				int firstOffset = (sideIndex * 8) % verts;
				int secondOffset = (sideIndex * 8 + 8) % verts;

				// vertex x and y coords
				const __m256 pX0 = _mm256_loadu_ps(m_VertRegX + firstOffset);
				const __m256 pY0 = _mm256_loadu_ps(m_VertRegY + firstOffset);
				const __m256 pX1 = _mm256_loadu_ps(m_VertRegX + secondOffset);
				const __m256 pY1 = _mm256_loadu_ps(m_VertRegY + secondOffset);

				// x difference (same for both rows) and y differences, from XY0 to P1 and to P2
				const __m256 X0vP = _mm256_sub_ps(pointX1, pX0);
				const __m256 Y0vP1 = _mm256_sub_ps(pointY1, pY0);
				const __m256 Y0vP2 = _mm256_sub_ps(pointY2, pY0);

				// XY0 to XY1
				const __m256 X0vX1 = _mm256_sub_ps(pX1, pX0);
				const __m256 Y0vY1 = _mm256_sub_ps(pY1, pY0);

				// calculate both cross products using temp and fmsub
				const __m256 temp = _mm256_mul_ps(Y0vY1, X0vP);

				// XY0->XY1 x XY0->P1
				const __m256 cross1 = _mm256_fmsub_ps(X0vX1, Y0vP1, temp);
				// XY0->XY1 x XY0->P2
				const __m256 cross2 = _mm256_fmsub_ps(X0vX1, Y0vP2, temp);

				// verts for polys are anti-clockwise, so a positive cross product means the point is inside
				mask1 = _mm256_and_ps(mask1, _mm256_cmp_ps(cross1, zeros, _CMP_GE_OS));
				mask2 = _mm256_and_ps(mask2, _mm256_cmp_ps(cross2, zeros, _CMP_GE_OS));
				
				// check for early exit if all points in both masks are outside
				int as1 = _mm256_movemask_ps(mask1);
				int as2 = _mm256_movemask_ps(mask2);
				
				// if all points are outside, go to the next set of points, but starting with the same edge which disproved the current batch
				// idea is that adjacent points will likely be proved all outside by the same edge
				if (as1 == 0 && as2 == 0)
				{
					full = false;
					break;
				}
				// otherwise check the next edge of the poly
				++sideIndex;
			}

			// OR the masks with the current grid to not make already filled cells empty
			__m256i maskInt1 = _mm256_castps_si256(mask1);
			__m256i maskInt2 = _mm256_castps_si256(mask2);
			__m256i* newaddr1 = (__m256i*)(grid + j / 8 * SCREEN_WIDTH + i);
			__m256i* newaddr2 = (__m256i*)(grid + (j + 8) / 8 * SCREEN_WIDTH + i);

			// no worries for concurrency issues, each thread works on its own part of the grid
			_mm256_storeu_si256(newaddr1, _mm256_or_si256(maskInt1, _mm256_loadu_si256(newaddr1)));
			_mm256_storeu_si256(newaddr2, _mm256_or_si256(maskInt2, _mm256_loadu_si256(newaddr2)));
		}
	}
	return full;
}

// circle and box collisions not yet optimised fully to the same level as polygon, would follow a similar pattern
bool CircleCollisionComponent::CheckPointsInCollider(int* grid, float* xPoints, float* yPoints)
{
	const __m256 centerX = _mm256_set1_ps(GetPos().x);
	const __m256 centerY = _mm256_set1_ps(GetPos().y);
	const __m256 radiusSq = _mm256_set1_ps(m_Radius * m_Radius);

	for (int j = 0; j < PATCH_SIZE*8; j +=16)
	{
		_mm_prefetch((char*)(yPoints + j + 16), _MM_HINT_T1);
		_mm_prefetch((char*)(yPoints + j + 24), _MM_HINT_T1);
		for (int i = 0; i < PATCH_SIZE; i += 8)
		{
			// Load 8 points into SIMD registers
			__m256 pointX = _mm256_loadu_ps(xPoints + i);
			__m256 pointY = _mm256_loadu_ps(yPoints + j);
			__m256 pointY2 = _mm256_loadu_ps(yPoints + j + 8);

			// Calculate squared distances
			__m256 dx = _mm256_sub_ps(pointX, centerX);
			__m256 dx2 = _mm256_mul_ps(dx, dx);
			__m256 dy = _mm256_sub_ps(pointY, centerY);
			__m256 dy2 = _mm256_sub_ps(pointY2, centerY);
			__m256 distSq = _mm256_fmadd_ps(dy, dy, dx2);
			__m256 distSq2 = _mm256_fmadd_ps(dy2, dy2, dx2);

			__m256 mask1 = _mm256_cmp_ps(distSq, radiusSq, _CMP_LE_OQ);
			__m256 mask2 = _mm256_cmp_ps(distSq2, radiusSq, _CMP_LE_OQ);

			__m256i maskInt1 = _mm256_castps_si256(mask1);
			__m256i maskInt2 = _mm256_castps_si256(mask2);
			__m256i* newaddr1 = (__m256i*)(grid + j / 8 * SCREEN_WIDTH + i);
			__m256i* newaddr2 = (__m256i*)(grid + (j + 8) / 8 * SCREEN_WIDTH + i);

			__m256i check1 = _mm256_loadu_si256(newaddr1);
			__m256i check2 = _mm256_loadu_si256(newaddr2);
			_mm256_storeu_si256(newaddr1, _mm256_or_si256(maskInt1, check1));
			_mm256_storeu_si256(newaddr2, _mm256_or_si256(maskInt2, check2));
		}
	}
	return false;
}

// circle and box collisions not yet optimised fully to the same level as polygon, would follow a similar pattern
bool BoxCollisionComponent::CheckPointsInCollider(int* grid, float* xPoints, float* yPoints)
{
	float rotation = GetRot() * TO_RADIANS;
	const __m256 sin_vec = _mm256_set1_ps(std::sin(rotation));
	const __m256 cos_vec = _mm256_set1_ps(std::cos(rotation));
	const __m256 boxCenterX = _mm256_set1_ps(GetPos().x);
	const __m256 boxCenterY = _mm256_set1_ps(GetPos().y);
	const __m256 halfWidth = _mm256_set1_ps(m_HalfWidth);
	const __m256 halfHeight = _mm256_set1_ps(m_HalfHeight);

	for (int j = 0; j < PATCH_SIZE * 8; j += 8) 
	{
		for (int i = 0; i < PATCH_SIZE; i += 8) 
		{
			// Load 8 points into SIMD registers
			__m256 pointX = _mm256_loadu_ps(xPoints + i);
			__m256 pointY = _mm256_loadu_ps(yPoints + j);

			// Translate the point to the box's frame of reference
			__m256 x = _mm256_sub_ps(pointX, boxCenterX);
			__m256 y = _mm256_sub_ps(pointY, boxCenterY);

			__m256 rotatedX = _mm256_sub_ps(_mm256_mul_ps(x, cos_vec), _mm256_mul_ps(y, sin_vec));
			__m256 rotatedY = _mm256_add_ps(_mm256_mul_ps(x, sin_vec), _mm256_mul_ps(y, cos_vec));

			// Check if the rotated point is within the box's bounds
			x = _mm256_and_ps(_mm256_cmp_ps(rotatedX, _mm256_sub_ps(_mm256_setzero_ps(), halfWidth), _CMP_GE_OQ),
				_mm256_cmp_ps(rotatedX, halfWidth, _CMP_LE_OQ));
			y = _mm256_and_ps(_mm256_cmp_ps(rotatedY, _mm256_sub_ps(_mm256_setzero_ps(), halfHeight), _CMP_GE_OQ),
				_mm256_cmp_ps(rotatedY, halfHeight, _CMP_LE_OQ));
			__m256 mask = _mm256_and_ps(x, y);

			__m256i maskInt = _mm256_castps_si256(mask);
			__m256i* newaddr = (__m256i*)(grid + j / 8 * SCREEN_WIDTH + i);
			__m256i check = _mm256_loadu_si256(newaddr);
			_mm256_storeu_si256(newaddr, _mm256_or_si256(maskInt, check));
		}
	}
	return false;
}
#endif
