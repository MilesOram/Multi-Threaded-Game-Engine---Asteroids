#include "ObjectPool.h"
#include "PlayerShip.h"
#include "Components.h"
#include "Gamestate.h"
#include "Projectile.h"

const uint16_t PlayerShip::DefaultCollisionTagsSelf{ 0b1000000000000000 };
const uint16_t PlayerShip::DefaultCollisionTagsOther{ 0b0100000000000000 };

PlayerShip::PlayerShip(sf::Texture& tex)
{
    m_Sprite = sf::Sprite(tex);
    m_Sprite.setOrigin((sf::Vector2f)tex.getSize() / 2.f);
    SetBothPositions({ SCREEN_WIDTH / 2 , SCREEN_HEIGHT / 2 });
    SetBothRotations(0);
}
PlayerShip::PlayerShip(const PlayerShip& other) : GameObject(other)
{
    m_VelX = other.m_VelX;
    m_VelY = other.m_VelY;
    m_ProjectileCD = other.m_ProjectileCD;
    m_TimeSincelastProjectile = other.m_TimeSincelastProjectile;
    m_TimeSinceInvulnBegin = other.m_TimeSinceInvulnBegin;
    m_Lives = other.m_Lives;
}
void PlayerShip::HandleInputs(float deltaTime)
{
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left))
    {
        RotateLeft(deltaTime);
    }
    else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right))
    {
        RotateRight(deltaTime);
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Up))
    {
        AccelerateForward(deltaTime);
    }
    else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Down))
    {
        Decelerate(deltaTime);
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Space))
    {
        if (m_TimeSincelastProjectile > m_ProjectileCD)
        {
            FireProjectile();
        }
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Q) && m_TimeSinceLightSwitch > 1.f)
    {
        m_TimeSinceLightSwitch = 0;
        if (!m_LightMax && m_LightTimer > .5f)
        {
            Gamestate::instance->SignalLightUp();
            m_LightMax = true;
        }
        else if (m_LightMax)
        {
            m_LightMax = false;
            Gamestate::instance->SignalLightDown();
        }
    }
}

void PlayerShip::Update(float deltaTime)
{
    // Inputs
    HandleInputs(deltaTime);

    // Timers
    m_TimeSincelastProjectile += deltaTime;
    m_TimeSinceInvulnBegin += deltaTime;
    m_TimeSinceLightSwitch += deltaTime;

    // Update position and wrap to screen if needed
    sf::Vector2f newPos = m_Position + sf::Vector2f(m_VelX * deltaTime, m_VelY * deltaTime);
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

    if (m_LightMax)
    {
        m_LightTimer -= deltaTime;
        if (m_LightTimer < 0)
        {
            m_LightTimer = 0;
            m_LightMax = false;
            Gamestate::instance->SignalLightDown();
        }
    }
    else
    {
        if (m_LightTimer < m_MaxLightTime) m_LightTimer += deltaTime;
    }

    if (m_TimeSinceInvulnBegin > m_InvulnTimer && GetComponent<CollisionComponent>()->GetSelfTag() == 0)
    {
        GetComponent<CollisionComponent>()->SetSelfTag(DefaultCollisionTagsSelf);
    }
    m_Position = newPos;
    Decay(deltaTime);
    GetComponent<CollisionComponent>()->UpdateInCollisionGrid();
}

void PlayerShip::RotateLeft(float deltaTime)
{
    m_Rotation -= deltaTime * m_RotationSpeed;
}
void PlayerShip::RotateRight(float deltaTime)
{
    m_Rotation += deltaTime * m_RotationSpeed;
}

void PlayerShip::AccelerateForward(float deltaTime)
{
    float new_x = m_VelX + deltaTime * std::sin(m_Rotation * TO_RADIANS) * m_Acceleration;
    float new_y = m_VelY - deltaTime * std::cos(m_Rotation * TO_RADIANS) * m_Acceleration;
    float speedSquared = new_x * new_x + new_y * new_y;
    if (speedSquared > m_MaxSpeed * m_MaxSpeed)
    {
        // Normalize the velocity to the maximum speed
        float speed = std::sqrt(speedSquared);
        new_x = (new_x / speed) * m_MaxSpeed;
        new_y = (new_y / speed) * m_MaxSpeed;
    }
    m_VelX = new_x;
    m_VelY = new_y;
}
void PlayerShip::Decelerate(float deltaTime)
{
    float new_x = m_VelX - deltaTime * std::sin(m_Rotation * TO_RADIANS) * m_Acceleration;
    float new_y = m_VelY + deltaTime * std::cos(m_Rotation * TO_RADIANS) * m_Acceleration;
    float speedSquared = new_x * new_x + new_y * new_y;
    if (speedSquared > m_MaxSpeed * m_MaxSpeed)
    {
        // Normalize the velocity to the maximum speed
        float speed = std::sqrt(speedSquared);
        new_x = (new_x / speed) * m_MaxSpeed;
        new_y = (new_y / speed) * m_MaxSpeed;
    }
    m_VelX = new_x;
    m_VelY = new_y;
}

void PlayerShip::Decay(float deltaTime)
{
    m_VelX -= (m_VelX > 0 ? 1 : -1) * m_DecayRate * deltaTime;
    m_VelY -= (m_VelY > 0 ? 1 : -1) * m_DecayRate * deltaTime;
}

void PlayerShip::FireProjectile()
{
    std::shared_ptr<GameObject> proj = Gamestate::instance->GetPooledObject(ProjectilePoolName);
    sf::Vector2f offset = sf::Vector2f(std::cos((m_Rotation+90) * TO_RADIANS) * 20, std::sin((m_Rotation+90) * TO_RADIANS) * 20);
    proj->ReinitialiseObject(m_Position - offset, m_Rotation, proj);
    m_TimeSincelastProjectile = 0;    
}

void PlayerShip::HandleCollision(uint16_t otherTags)
{
    if (!GetActive() || m_TimeSinceInvulnBegin < m_InvulnTimer) { return; }
    if (--m_Lives < 0)
    {
        std::cout << "Game Over." << std::endl;
        SetInactive();
    }
    else
    {
        // return to centre of screen and make invuln
        std::cout << "Hit." << std::endl;
        SetBothPositions({ SCREEN_WIDTH/2,SCREEN_HEIGHT/2 });
        SetBothRotations(0);
        m_VelX = 0; 
        m_VelY = 0;
        GetComponent<CollisionComponent>()->SetSelfTag(0);
        m_TimeSinceInvulnBegin = 0;
    }
}
std::shared_ptr<GameObject> PlayerShip::CloneToSharedPtr()
{
    std::shared_ptr<GameObject> obj = std::make_shared<PlayerShip>(*this);
    obj->CloneComponentsFromOther(obj, this);
    return obj;
}
