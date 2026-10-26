/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <vector>
#include <algorithm>
#include <cstddef>

struct Point2D_t {
    int x = 0;
    int y = 0;

    bool operator==(const Point2D_t& other) const
    {
        return x == other.x && y == other.y;
    }
};

/**
 * @brief Abstract interface for keyboard reading implementations
 */
class KeyboardReader {
public:
    static constexpr std::size_t PhysicalKeyCount = 56;

    // Fixed for this reader's lifetime. Reserve before the first hardware sample.
    explicit KeyboardReader(std::size_t max_keys = PhysicalKeyCount)
        : _max_keys(std::min(max_keys, PhysicalKeyCount))
    {
        _key_list.reserve(_max_keys);
    }
    virtual ~KeyboardReader() = default;
    std::size_t maxKeys() const { return _max_keys; }

    /**
     * @brief Initialize the keyboard reader
     */
    virtual void begin()
    {
    }

    /**
     * @brief Update the keyboard state and key list
     */
    virtual void update()
    {
    }

    /**
     * @brief Get the current key list
     * @return Reference to the key list
     */
    inline const std::vector<Point2D_t>& keyList() const
    {
        return _key_list;
    }

protected:
    // Duplicate/invalid presses and presses beyond the fixed limit are ignored.
    // Releases must still be accepted when the list is full.
    void setKey(const Point2D_t& point, bool pressed)
    {
        if (point.x < 0 || point.x >= 14 || point.y < 0 || point.y >= 4) return;
        const auto found = std::find(_key_list.begin(), _key_list.end(), point);
        if (pressed) {
            if (found == _key_list.end() && _key_list.size() < _max_keys) _key_list.push_back(point);
        } else if (found != _key_list.end()) {
            _key_list.erase(found);
        }
    }
    std::vector<Point2D_t> _key_list;

private:
    const std::size_t _max_keys;
};
