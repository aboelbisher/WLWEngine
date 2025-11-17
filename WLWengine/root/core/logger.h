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
    }()) \


#ifdef WLW_USE_GLFW


//#include <gl/glu.h> // Required for gluErrorString
//#include <gl/gl.h> // Required for gluErrorString
//#include <GLFW/glfw3.h>
// 
//GLenum _glCheckError(const char* file, int line) {
//  GLenum errorCode;
//
//  // Loop to retrieve all outstanding errors (glGetError clears the flag upon call)
//  while ((errorCode = glGetError()) != GL_NO_ERROR) {
//    std::string error;
//    std::string description;
//
//    switch (errorCode) {
//    case GL_INVALID_ENUM:
//      error = "GL_INVALID_ENUM";
//      description = "An unacceptable value is specified for an enumerated argument.";
//      break;
//    case GL_INVALID_VALUE:
//      error = "GL_INVALID_VALUE";
//      description = "A numeric argument is out of range.";
//      break;
//    case GL_INVALID_OPERATION:
//      error = "GL_INVALID_OPERATION";
//      description = "The specified operation is not allowed in the current state.";
//      break;
//    case GL_STACK_OVERFLOW:
//      error = "GL_STACK_OVERFLOW";
//      description = "A stack pushing operation attempted to push onto a full stack.";
//      break;
//    case GL_STACK_UNDERFLOW:
//      error = "GL_STACK_UNDERFLOW";
//      description = "A stack popping operation attempted to pop from an empty stack.";
//      break;
//    case GL_OUT_OF_MEMORY:
//      error = "GL_OUT_OF_MEMORY";
//      description = "There is not enough memory left to execute the command.";
//      break;
//    default:
//      error = "UNKNOWN_ERROR";
//      description = "An unknown error code was returned.";
//      break;
//    }
//
//    // Print the detailed error message
//    std::cerr << "\n"
//      << " OpenGL Error: " << error << " (0x" << std::hex << errorCode << std::dec << ")\n"
//      << "   Description: " << description << "\n"
//      << "   Location: " << file << " (" << line << ")\n"
//      << std::endl;
//  }
//  return errorCode;
//}
//
//
//// --- Debug Macro Definition ---
//
//// We define this macro only if NDEBUG (No Debug) is NOT defined (i.e., in Debug builds).
//// In Release builds, this macro will be defined as nothing (a no-op) to save performance.
//#ifndef NDEBUG
//    // The macro uses the built-in C++ preprocessor constants __FILE__ (string) 
//    // and __LINE__ (integer) to pass the location of the call site to the helper function.
//#define GL_CHECK_ERROR() _glCheckError(__FILE__, __LINE__)
//#else
//#define GL_CHECK_ERROR()
//#endif
//
//


#endif // WLW_USE_GLFW