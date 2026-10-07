#pragma once

// Maximum number of expanded vertices kept in the linear-memory staging arrays
// for one batch. The renderer drains the GPU before reusing the arrays.
static const u32 CTR_VERTEX_BUFFER_CAPACITY = 0x40000;
