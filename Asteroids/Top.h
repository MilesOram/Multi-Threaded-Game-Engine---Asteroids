#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#include <random>
#include <set>

#define USE_CPU_FOR_OCCLUDERS false
#define FRAME_RATE_LIMIT 360

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1920
#define GRID_RESOLUTION 24
#define NUM_THREADS 8
#define M_PI 3.14159265
const int PATCH_SIZE = SCREEN_WIDTH / GRID_RESOLUTION;

static_assert(SCREEN_WIDTH % GRID_RESOLUTION == 0);

#if USE_CPU_FOR_OCCLUDERS
// method only set up to work with certain screen resolutions
static_assert(SCREEN_WIDTH == SCREEN_HEIGHT);
static_assert(PATCH_SIZE % 8 == 0);
#endif

const float TO_RADIANS = 0.0174532f;
const sf::Time MIN_FRAME_TIME = sf::seconds(1.f / FRAME_RATE_LIMIT);

int random_int(int range_lower, int range_upper);
