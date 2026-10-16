#pragma once

// Vert+ for a game area taller than 16:9 (16:10, 4:3): the vertical field of view that keeps the horizontal
// view the game has at 16:9, so the extra height shows more of the scene above and below. Returns fovy
// unchanged for 16:9 and wider aspects, and for values that are not a screen camera's field of view.
float aspect_vert_plus(float fovy, float aspect);
