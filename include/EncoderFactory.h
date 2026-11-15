// EncoderFactory.h
#pragma once
#include "IEncoder.h"
#include <memory>

class EncoderFactory {
public:
    enum class Type { NVENC, QSV, X264 };

    static std::unique_ptr<IEncoder> Create(int width, int height, int fps,
        int bitrateBps, int gopSize);
    static Type Detect();
};