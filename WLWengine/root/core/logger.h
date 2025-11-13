#pragma once

#include <iostream>


#define FAIL_IF(CONDITION, MESSAGE) \
    ([&]() -> bool { \
        if ((CONDITION)) { \
            std::cout << "  Custom Message: " << MESSAGE << "\n" \
                      << "  Condition: " << #CONDITION << "\n" \
                      << "  File: " << __FILE__ << "\n" \
                      << "  Line: " << __LINE__ << "\n" \
                      << "  Function: " << __func__ << "\n" \
                      << "-------------------------------\n"; \
            return true; \
        } else { \
            return false; \
        } \
    }())