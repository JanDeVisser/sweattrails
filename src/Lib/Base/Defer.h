/*
 * Copyright (c) ${YEAR}, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace ST {

template<typename Callback>
class Defer {
public:
    explicit Defer(Callback const &callback)
        : m_callback(callback)
    {
    }

    ~Defer()
    {
        m_callback();
    }

private:
    Callback const &m_callback;
};

}
