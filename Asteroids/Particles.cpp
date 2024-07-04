#include "Particles.h"


ParticleSystem::ParticleSystem(unsigned int count) :
	m_Particles(count),
	m_Vertices(sf::Points, count),
	m_Lifetime(sf::seconds(1.5f)),
	m_Emitter(0, 0)
{}

void ParticleSystem::SetEmitter(sf::Vector2f position)
{
	m_Emitter = position;
}

void ParticleSystem::Update(sf::Time elapsed, int start, int end, float exhaustAngle)
{
	const float oneOverLifetimeSeconds = 1.f / m_Lifetime.asSeconds();
	const float elapsedSeconds = elapsed.asSeconds();
	for (int i = start; i < end; ++i)
	{
		// update the particle lifetime
		Particle& p = m_Particles[i];
		p.lifetime -= elapsed;

		// if the particle is dead, respawn it
		if (p.lifetime <= sf::Time::Zero) ResetParticle(i, exhaustAngle);

		// update the position of the corresponding vertex
		auto& vert = m_Vertices[i];
		vert.position += p.velocity * elapsedSeconds;

		// update the alpha (transparency) of the particle according to its lifetime
		float ratio = p.lifetime.asSeconds() * oneOverLifetimeSeconds;
		vert.color.a = static_cast<sf::Uint8>(ratio * 255);
	}
}

void ParticleSystem::draw(sf::RenderTarget& target, sf::RenderStates states) const
{
	// apply the transform
	states.transform *= getTransform();

	// our particles don't use a texture
	states.texture = NULL;

	// draw the vertex array
	target.draw(m_Vertices, states);
}

void ParticleSystem::ResetParticle(std::size_t index, float exhaustAngle)
{
	// give a random velocity and lifetime to the particle
	float angle = ((std::rand() % 60) - 30 + exhaustAngle) * TO_RADIANS;
	float speed = (std::rand() % 100) + 50.f;
	m_Particles[index].velocity = sf::Vector2f(std::cos(angle) * speed, std::sin(angle) * speed);
	m_Particles[index].lifetime = sf::milliseconds((std::rand() % 1000) + 500);

	// reset the position of the corresponding vertex
	m_Vertices[index].position = m_Emitter;
}
