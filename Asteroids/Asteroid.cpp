#include "ObjectPool.h"
#include "Asteroid.h"
#include "Components.h"
#include "Gamestate.h"

// collision tags
const uint16_t Asteroid::DefaultCollisionTagsSelf{ 0b0110000000000000 };
const uint16_t Asteroid::DefaultCollisionTagsOther{ 0b1001000000000000 };

// pool names
const char Asteroid::AsteroidSmallPoolName[14] = "AsteroidSmall";
const char Asteroid::AsteroidMediumPoolName[15] = "AsteroidMedium";
const char Asteroid::AsteroidLargePoolName[14] = "AsteroidLarge";

// texture atlas fixed coords
const sf::Vector2f Asteroid::TextureAtlasSmallTL = {400,0};
const sf::Vector2f Asteroid::TextureAtlasMediumTL = { 200,0 };
const sf::Vector2f Asteroid::TextureAtlasLargeTL = { 0,0 };
const sf::Vector2f Asteroid::TextureAtlasSmallBR = {473,69};
const sf::Vector2f Asteroid::TextureAtlasMediumBR = { 295,90 };
const sf::Vector2f Asteroid::TextureAtlasLargeBR = { 156,144 };

Asteroid::Asteroid(sf::Texture& tex, AST_SIZE size) : m_Size(size)
{
    m_Sprite = sf::Sprite(tex);
    m_Sprite.setOrigin((sf::Vector2f)tex.getSize() / 2.f);

}
Asteroid::Asteroid(const Asteroid& other) : GameObject(other)
{
    m_RotationSpeed = other.m_RotationSpeed;
    m_XDrift = other.m_XDrift;
    m_YDrift = other.m_YDrift;
    m_Size = other.m_Size;
}
void Asteroid::Update(float deltaTime)
{
    if (!m_Active) return;
    // update position, then check if off screen, if so wrap
    // check lifetime since asteroids start off screen, so let them move in before wrap checking
    sf::Vector2f newPos = m_Position + sf::Vector2f(m_XDrift * deltaTime, m_YDrift * deltaTime);
    if (m_Lifetime > 3)
    {
        if (newPos.x < 0)
        {
            newPos.x += SCREEN_WIDTH;
        }
        else if (newPos.x > SCREEN_WIDTH)
        {
            newPos.x -= SCREEN_WIDTH;
        }
        if (newPos.y < 0)
        {
            newPos.y += SCREEN_HEIGHT;
        }
        else if (newPos.y > SCREEN_HEIGHT)
        {
            newPos.y -= SCREEN_HEIGHT;
        }
    }
    // update the relevant members and then collision grid
    m_Position = newPos;
    m_Rotation += m_RotationSpeed * deltaTime;
    m_Lifetime += deltaTime;
    GetComponent<CollisionComponent>()->UpdateInCollisionGrid();
}

void Asteroid::Split()
{
    SetInactive();
    switch (m_Size)
    {
    case AST_SIZE::large:
        SpawnMediums();
        break;
    case AST_SIZE::medium:
        SpawnSmalls();
        break;
    case AST_SIZE::small:
        break;
    }
    Gamestate::instance->AddScore(m_Score);
    Gamestate::instance->AddToCleanupObjects(GetComponent<CollisionComponent>()->GetParentSharedPtr());
}

void Asteroid::SpawnMediums() const
{
    float offsetMultiplier = 40;
    sf::Vector2f offset = { std::sin(GetRotation() * TO_RADIANS) * offsetMultiplier, -std::cos(GetRotation() * TO_RADIANS) * offsetMultiplier };

    std::shared_ptr<GameObject> ast = Gamestate::instance->GetPooledObject(AsteroidMediumPoolName);
    std::shared_ptr<GameObject> ast2 = Gamestate::instance->GetPooledObject(AsteroidMediumPoolName);
    ast->ReinitialiseObject(offset + m_Position, 0, ast);
    ast2->ReinitialiseObject(-offset + m_Position, 0, ast2);
}
void Asteroid::SpawnSmalls() const
{
    float offsetMultiplier = 25;
    sf::Vector2f offset = { std::sin(GetRotation() * TO_RADIANS) * offsetMultiplier, -std::cos(GetRotation() * TO_RADIANS) * offsetMultiplier };

    std::shared_ptr<GameObject> ast = Gamestate::instance->GetPooledObject(AsteroidSmallPoolName);
    std::shared_ptr<GameObject> ast2 = Gamestate::instance->GetPooledObject(AsteroidSmallPoolName);
    ast->ReinitialiseObject(offset + m_Position, 0, ast);
    ast2->ReinitialiseObject(-offset + m_Position, 0, ast2);
}
void Asteroid::HandleCollision(uint16_t otherTags)
{
    if (!GetActive()) { return; }
    // can reach here with collision with player so need to specify tag deals damage to this i.e. projectile in order to cause split
    if ((otherTags & 0x1000) > 0)
    {
        Split();
    }
}

void Asteroid::Reinitialise()
{
    m_XDrift = m_Position.x > (SCREEN_WIDTH / 2) ? static_cast<float>(random_int(-80, -20)) : static_cast<float>(random_int(20, 80));
    m_YDrift = m_Position.y > (SCREEN_HEIGHT / 2) ? static_cast<float>(random_int(-80, -20)) : static_cast<float>(random_int(20, 80));
    m_Lifetime = 0;
}
std::shared_ptr<GameObject> Asteroid::CloneToSharedPtr()
{
    std::shared_ptr<GameObject> obj = std::make_shared<Asteroid>(*this);
    obj->CloneComponentsFromOther(obj, this);
    return obj;
}

sf::Vector2f Asteroid::GetTextureAtlasOffsetTL() const
{
    switch (m_Size)
    {
    case AST_SIZE::small:
        return TextureAtlasSmallTL;
    case AST_SIZE::medium:
        return TextureAtlasMediumTL;
    case AST_SIZE::large:
        return TextureAtlasLargeTL;
    }
    return { 0,0 };
}
sf::Vector2f Asteroid::GetTextureAtlasOffsetBR() const
{
    switch (m_Size)
    {
    case AST_SIZE::small:
        return TextureAtlasSmallBR;
    case AST_SIZE::medium:
        return TextureAtlasMediumBR;
    case AST_SIZE::large:
        return TextureAtlasLargeBR;
    }
    return { 0,0 };
}
