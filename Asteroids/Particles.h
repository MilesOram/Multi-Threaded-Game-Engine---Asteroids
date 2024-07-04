#pragma once
#include "Top.h"

class ParticleSystem : public sf::Drawable, public sf::Transformable
{
public:
	ParticleSystem(unsigned int count);

	void SetEmitter(sf::Vector2f position);
	void Update(sf::Time elapsed, int start, int end, float exhaustAngle);
	size_t GetParticleCount() const { return m_Particles.size(); }

private:
	struct Particle
	{
		sf::Vector2f velocity;
		sf::Time lifetime;
	};

	std::vector<Particle> m_Particles;
	sf::VertexArray m_Vertices;
	sf::Time m_Lifetime;
	sf::Vector2f m_Emitter;

	virtual void draw(sf::RenderTarget& target, sf::RenderStates states) const;

	void ResetParticle(std::size_t index, float exhaustAngle);
};