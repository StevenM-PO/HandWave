/* The single translation unit that compiles miniaudio's implementation.
 * Everywhere else includes miniaudio.h for declarations only. */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
/* Real-time audio thread priority (SCHED_FIFO). 80 stays below the kernel's own
 * critical threads; miniaudio would otherwise use the maximum, 99. */
#define MA_PTHREAD_REALTIME_THREAD_PRIORITY 80
#include "miniaudio.h"
