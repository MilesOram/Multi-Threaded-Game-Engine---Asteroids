#include "ObjectPool.h"
#include "Projectile.h"
#include "Components.h"
#include "Gamestate.h"

const uint16_t Projectile::DefaultCollisionTagsSelf{ 0b1001000000000000 };
const uint16_t Projectile::DefaultCollisionTagsOther{ 0b0110000000000000 };

Projectile::Projectile(sf::Texture& _Texture)
{
    m_Sprite = sf::Sprite(_Texture);
}
Projectile::Projectile(const Projectile& other) : GameObject(other)
{
    m_VelX = other.m_VelX;
    m_VelY = other.m_VelY;
}

void Projectile::Update(float deltaTime)
{
    if (!m_Active) return;
    if (m_Position.x < 5.f || m_Position.x > SCREEN_WIDTH - 5.f || m_Position.y < 5.f || m_Position.y > SCREEN_HEIGHT - 5.f)
    {
        // remove if it goes off screen, does not wrap
        SetInactive();
        Gamestate::instance->AddToCleanupObjectsDelayed(GetComponent<CollisionComponent>()->GetParentSharedPtr());
    }
    else
    {
        m_Position.x += m_VelX * deltaTime;
        m_Position.y -= m_VelY * deltaTime;
    }
    GetComponent<CollisionComponent>()->UpdateInCollisionGrid();
}

void Projectile::HandleCollision(uint16_t otherTags)
{
    if (!GetActive()) { return; }
    SetInactive();
    Gamestate::instance->AddToCleanupObjects(GetComponent<CollisionComponent>()->GetParentSharedPtr());
}

void Projectile::CalculateXandYVelocity()
{
    m_VelX = m_Speed * sinf(m_Rotation * TO_RADIANS);
    m_VelY = m_Speed * cosf(m_Rotation * TO_RADIANS);
}

void Projectile::Reinitialise()
{
    CalculateXandYVelocity();
}
std::shared_ptr<GameObject> Projectile::CloneToSharedPtr()
{
    std::shared_ptr<GameObject> obj = std::make_shared<Projectile>(*this);
    obj->CloneComponentsFromOther(obj, this);
    return obj;
}
