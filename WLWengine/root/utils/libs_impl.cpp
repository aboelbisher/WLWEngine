// Define these macros to enable the implementation code
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

// Define this to avoid tiny_gltf including these files twice if we aren't careful
// (Though tiny_gltf typically handles guards well, this order is safest)
#include "tiny_gltf.h"

// Note: tiny_gltf.h internally includes "stb_image.h", "stb_image_write.h", and "json.hpp"
// So you don't need to include them manually here.