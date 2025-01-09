#pragma once
#include_next <board.h>

// Undefine the KB family variant.
#undef AT32F415KB

// Add a definition to describe this,
// project uses the RC family variant.
#define AT32F415RC
