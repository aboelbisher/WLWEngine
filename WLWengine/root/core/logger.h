#pragma once

#include <iostream>


#define FAIL_IF(CONDITION, MESSAGE) \
    ([&]() -> bool { \
        if (!(CONDITION)) { \
            std::cerr << "--- CHECK FAILED (LOG ONLY) ---\n" \
                      << "  Custom Message: " << MESSAGE << "\n" \
                      << "  Condition: " << #CONDITION << "\n" \
                      << "  File: " << __FILE__ << "\n" \
                      << "  Line: " << __LINE__ << "\n" \
                      << "  Function: " << __func__ << "\n" \
                      << "-------------------------------\n"; \
            return false; \
        } else { \
            return true; \
        } \
    }())