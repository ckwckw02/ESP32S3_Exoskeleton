#ifndef EQUATION_H
#define EQUATION_H

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Gait-geometry model (eq8-eq14) for the exoskeleton. Pure math with no
// hardware or Arduino dependencies, so it can be compiled and unit-tested on a
// PC as-is (add a main() that calls motor_equation()).
//
// Inputs : T(s), SL(cm), L(cm), Lp(cm), LD(cm), H(cm);
//          pd: 1 = PD (alpha from gait), 0 = healthy (alpha = 0.52 rad)
// Outputs: lift/release ERPM and phase times, plus dL for debug display.
// ============================================================================

typedef struct {
  int32_t  lift_rpm;     // ERPM for lift    (to-Pos phases),   eq13 revised
  int32_t  release_rpm;  // ERPM for release (to-Zero phases),  eq14 revised
  uint32_t lift_ms;      // lift time    = 0.4*T*1000 ms
  uint32_t release_ms;   // release time = 0.6*T*1000 ms
  float    dL;           // computed cable travel (cm), for debug display
  bool     valid;        // false when inputs are invalid (rpm forced to 0)
} equation_result_t;

// T(s) SL(cm) L(cm) Lp LD H(cm); pd: 1=PD(alpha from gait), 0=healthy(alpha=0.52 rad)
static inline void motor_equation(double T, double SL, double L, double Lp, double LD, double H, int pd, equation_result_t *out) {
  out->valid = false;
  out->lift_rpm = 0;
  out->release_rpm = 0;
  out->dL = 0.0f;

  // Guards: valid step time and positive dimensions (avoid div-by-zero / NaN).
  if (!(T > 0.1) || !(SL > 0.0) || !(L > 0.0)) return;

  // eq11: PD or healthy
  double al = pd ? asin(SL/(2*L)) : 0.52;

  // eq8 revised
  double Lo = sqrt(H*H + (Lp+LD)*(Lp+LD));

  // eq9 sides
  double a = LD + Lp/cos(al), b = H + Lp*tan(al);

  // eq10 (cos(pi/2-a)=sin a) — the sqrt argument must stay non-negative.
  double rad2 = a*a + b*b - 2*a*b*sin(al);
  if (rad2 < 0) return;
  double dL = Lo - sqrt(rad2);

  // NaN guard: invalid geometry (e.g. SL > 2L makes asin -> NaN) lands here.
  if (!(dL > 0)) return;

  out->dL = (float)dL;

  // rpm and time — the x6 x14 factor converts the plain formula to ERPM for this linkage.
  out->lift_rpm    = (int32_t)llround(6.36*dL/(0.4*T)*6*14);   // eq13 revised
  out->release_rpm = (int32_t)llround(6.36*dL/(0.6*T)*6*14);   // eq14 revised
  out->lift_ms     = (uint32_t)(0.4*T*1000);
  out->release_ms  = (uint32_t)(0.6*T*1000);
  out->valid       = true;
}

#endif // EQUATION_H
