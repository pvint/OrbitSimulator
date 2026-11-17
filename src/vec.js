// Tiny 2D vector helpers. Vectors are plain objects {x, y}.
// Each function returns a new vector rather than modifying its inputs.

export const vec = (x, y) => ({ x, y });
export const add = (a, b) => ({ x: a.x + b.x, y: a.y + b.y });
export const sub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y });
export const scale = (a, s) => ({ x: a.x * s, y: a.y * s });
export const dot = (a, b) => a.x * b.x + a.y * b.y;

/** z-component of the 3D cross product a × b. Positive when b is
 *  counter-clockwise from a. For position × velocity it is the angular momentum per unit mass. */
export const cross = (a, b) => a.x * b.y - a.y * b.x;

export const length = (a) => Math.hypot(a.x, a.y);

/** Unit vector in the direction of a, or the zero vector if a is zero. */
export function normalize(a) {
  const len = length(a);
  return len > 0 ? scale(a, 1 / len) : vec(0, 0);
}
