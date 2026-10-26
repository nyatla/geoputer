/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "Keyboard.h"
#include "../common.h"
#include "KeyboardReader/KeyboardReader.h"
#include "KeyboardReader/IOMatrix.h"
#include "KeyboardReader/TCA8418.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <memory>

Keyboard_Class::Keyboard_Class(std::size_t max_keys)
    : _max_keys(std::min(max_keys, KeyboardReader::PhysicalKeyCount)),
      _is_caps_locked(false), _last_key_size(0)
{
    _keys_state_buffer.modifier_keys.reserve(std::min(_max_keys, std::size_t{8}));
    _keys_state_buffer.hid_keys.reserve(_max_keys);
    _keys_state_buffer.word.reserve(_max_keys);
}

void Keyboard_Class::begin()
{
    auto board_type = M5.getBoard();
    _keyboard_reader.reset();

    // Reader injection based on board type
    if (board_type == m5::board_t::board_M5Cardputer) {
        _keyboard_reader = std::make_unique<IOMatrixKeyboardReader>(_max_keys);
    } else if (board_type == m5::board_t::board_M5CardputerADV) {
        _keyboard_reader = std::make_unique<TCA8418KeyboardReader>(-1, _max_keys);
    } else {
        printf("[error] Keyboard: Unsupported board type: %d\n", (int)board_type);
        _keyboard_reader = std::make_unique<KeyboardReader>(_max_keys);
    }

    _keyboard_reader->begin();
}

void Keyboard_Class::begin(std::unique_ptr<KeyboardReader> reader)
{
    // Reject an incompatible injected reader before it can grow state vectors.
    if (reader && reader->maxKeys() > _max_keys) return;
    _keyboard_reader = std::move(reader);
    if (_keyboard_reader) _keyboard_reader->begin();
}

uint8_t Keyboard_Class::getKey(Point2D_t keyCoor)
{
    uint8_t ret = 0;

    if ((keyCoor.x < 0) || (keyCoor.y < 0)) {
        return 0;
    }
    if (_keys_state_buffer.ctrl || _keys_state_buffer.shift || _is_caps_locked) {
        ret = _key_value_map[keyCoor.y][keyCoor.x].value_second;
    } else {
        ret = _key_value_map[keyCoor.y][keyCoor.x].value_first;
    }
    return ret;
}

void Keyboard_Class::updateKeyList()
{
    if (_keyboard_reader) {
        _keyboard_reader->update();
    }
}

uint8_t Keyboard_Class::isPressed()
{
    return keyList().size();
}

bool Keyboard_Class::isChange()
{
    uint8_t current_size = keyList().size();
    if (_last_key_size != current_size) {
        _last_key_size = current_size;
        return true;
    } else {
        return false;
    }
}

bool Keyboard_Class::isKeyPressed(char c)
{
    const auto& keys = keyList();
    if (keys.size()) {
        for (const auto& i : keys) {
            if (getKey(i) == c) return true;
        }
    }
    return false;
}

#include <cstring>

void Keyboard_Class::updateKeysState()
{
    // printf("-------------------\n");

    _keys_state_buffer.reset();

    const auto& keys       = keyList();
    // =================================================================
    // PASS 1: Identify all active modifier keys first.
    // This ensures their state is known before processing other keys.
    // =================================================================
    for (const auto& key_pos : keys) {
        const uint8_t key_code = getKeyValue(key_pos).value_first;
        switch (key_code) {
            case KEY_FN:
                _keys_state_buffer.fn = true;
                break;
            case KEY_OPT:
                _keys_state_buffer.opt = true;
                break;
            case KEY_LEFT_CTRL:
                _keys_state_buffer.ctrl = true;
                _keys_state_buffer.modifiers |= (1 << (key_code - 0x80));
                _keys_state_buffer.modifier_keys.push_back(key_code);
                break;
            case KEY_LEFT_SHIFT:
                _keys_state_buffer.shift = true;
                _keys_state_buffer.modifiers |= (1 << (key_code - 0x80));
                _keys_state_buffer.modifier_keys.push_back(key_code);
                break;
            case KEY_LEFT_ALT:
                _keys_state_buffer.alt = true;
                _keys_state_buffer.modifiers |= (1 << (key_code - 0x80));
                _keys_state_buffer.modifier_keys.push_back(key_code);
                break;
                // Note: We only handle modifiers in this pass.
        }
    }

    // =================================================================
    // PASS 2: Process all non-modifier keys.
    // Now the modifier state is correct, regardless of key order.
    // =================================================================
    for (const auto& key_pos : keys) {
        const KeyValue_t key_value = getKeyValue(key_pos);
        const uint8_t key_code     = key_value.value_first;

        // printf("%d\n", key_code);

        // Skip modifier keys as they were handled in the first pass
        switch (key_code) {
            case KEY_FN:
            case KEY_OPT:
            case KEY_LEFT_CTRL:
            case KEY_LEFT_SHIFT:
            case KEY_LEFT_ALT:
                continue;  // Already processed
        }

        // Handle other special keys
        switch (key_code) {
            case KEY_TAB:
                _keys_state_buffer.tab = true;
                _keys_state_buffer.hid_keys.push_back(key_code);
                continue;  // Skip further processing for this key
            case KEY_BACKSPACE:
                _keys_state_buffer.del = true;
                _keys_state_buffer.hid_keys.push_back(key_code);
                continue;  // Skip further processing for this key
            case KEY_ENTER:
                _keys_state_buffer.enter = true;
                _keys_state_buffer.hid_keys.push_back(key_code);
                continue;  // Skip further processing for this key
        }

        // Handle printable keys (including space)
        if (key_code == ' ') {
            _keys_state_buffer.space = true;
        }


        // Add HID key to the list
        const uint8_t hid_key = _kb_asciimap[key_code];
        if (hid_key) {
            _keys_state_buffer.hid_keys.push_back(hid_key);
        }

        // Add character to the word buffer, now with the correct modifier state
        // printf("%d %d %d\n", _keys_state_buffer.ctrl, _keys_state_buffer.shift, _is_caps_locked);
        if (_keys_state_buffer.ctrl || _keys_state_buffer.shift || _is_caps_locked) {
            // printf("push_back %c\n", key_value.value_second);
            _keys_state_buffer.word.push_back(key_value.value_second);
        } else {
            // printf("push_back %c\n", key_value.value_first);
            _keys_state_buffer.word.push_back(key_value.value_first);
        }
    }
}
