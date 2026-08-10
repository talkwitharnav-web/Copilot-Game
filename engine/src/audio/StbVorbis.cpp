// stb_vorbis's implementation, alone in its own translation unit.
//
// It has to be compiled exactly once, and it cannot share a file with
// miniaudio's implementation: both are single-header libraries that pull in
// their own platform headers, and stb_vorbis defines helpers with names common
// enough to collide. Keeping them apart costs one file and removes the question.
//
// **The warning suppression is in CMake, not here.** A `#pragma warning(push, 0)`
// around the include looks like it should work and does not: C4701 is issued by
// the optimizer at the end of a function, after the pragma region has closed,
// so the only thing that covers it is a compile option on the file itself.
#include <stb_vorbis.c>
