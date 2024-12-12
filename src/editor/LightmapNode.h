#pragma once
#include "Texture.h"
#include <memory>

#include <memory>

class LightmapNode
{
public:
    std::unique_ptr<LightmapNode> child[2];
    int x, y, w, h;
    bool filled;

    LightmapNode(int offX, int offY, int mapW, int mapH)
        : x(offX), y(offY), w(mapW), h(mapH), filled(false)
    {
        child[0] = nullptr;
        child[1] = nullptr;
    }

    ~LightmapNode() = default;

    LightmapNode(LightmapNode&& other) noexcept
        : child{ std::move(other.child[0]), std::move(other.child[1]) },
        x(other.x), y(other.y), w(other.w), h(other.h), filled(other.filled)
    {
        other.x = other.y = other.w = other.h = 0;
        other.filled = false;
    }

    LightmapNode& operator=(LightmapNode&& other) noexcept
    {
        if (this != &other)
        {
            child[0] = std::move(other.child[0]);
            child[1] = std::move(other.child[1]);
            x = other.x;
            y = other.y;
            w = other.w;
            h = other.h;
            filled = other.filled;

            other.x = other.y = other.w = other.h = 0;
            other.filled = false;
        }
        return *this;
    }

    LightmapNode(const LightmapNode&) = delete;
    LightmapNode& operator=(const LightmapNode&) = delete;

    bool insert(int iw, int ih, int& outX, int& outY);
};
