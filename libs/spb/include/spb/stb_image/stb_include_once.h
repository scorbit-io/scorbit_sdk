// Warning: inlcude this file only ONCE in some .c or .cpp file.
// This will define it in one place, while other includes of stb_image.h and 
// stb_image_resize2.h will use this definition.
// We need this to avoid build issues due to ODR violation.

#pragma once

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

