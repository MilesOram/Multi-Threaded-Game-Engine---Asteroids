#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#include <random>
#include <set>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 800
#define GRID_RESOLUTION 12
#define NUM_THREADS 8
#define M_PI 3.14159265

const float TO_RADIANS = 0.0174532f;

int random_int(int range_lower, int range_upper);

struct PhaseBox
{
    int Left=0, Right=0, Top=0, Bottom=0;
    bool operator==(const PhaseBox& other) {
        return Left == other.Left && Right == other.Right && Top == other.Top && Bottom == other.Bottom;
    }
};