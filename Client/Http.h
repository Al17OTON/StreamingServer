#pragma once

/// @brief HTTP3 Interface
class Http {
public:
    virtual ~Http() = default;
    virtual void Send() = 0;
};