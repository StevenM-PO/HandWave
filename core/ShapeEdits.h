#pragma once

#include <vector>

namespace hw {

// Editing operations on a drawn shape.
//
// Shapes use the WaveCanvas convention: points.front() is the start of the
// cycle and points.back() is the end. Both sit at the same phase, approached
// from opposite sides, so points.back() != points.front() means there is a
// jump at the seam (like a saw). The cycle has points.size() - 1 unique points.
//
// These are plain functions with step-based amounts, so a touch gesture, a
// key press, or a hardware encoder can all drive them the same way.

// Shift the drawing around the cycle by `steps` points, wrapping at the seam.
// Positive moves it right (later in the cycle). The seam ends up at a point
// that was continuous, and the old seam jump moves inside the drawing.
//
// This doesn't change the sound of a single oscillator (the ear can't hear
// absolute phase). It is an editing aid, e.g. to move the seam to the middle
// so it can be redrawn by hand.
void rotatePhase(std::vector<float>& points, int steps);

// Make the end of the cycle meet its start. The jump at the seam is split
// evenly: each side is shifted toward the midpoint, by an amount that fades
// to zero over `blendFraction` of the cycle in total (half on each side).
// The rest of the drawing keeps its shape, and a continuous seam is left
// unchanged.
void joinEnds(std::vector<float>& points, float blendFraction);

} // namespace hw
