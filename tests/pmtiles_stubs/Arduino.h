#pragma once
#include <stdint.h>
#include <stddef.h>
#define FILE_READ 0

#include <stdio.h>
struct MockSerial {
    template<typename... Args> void printf(const char*, Args...) {}
};
static MockSerial Serial;
