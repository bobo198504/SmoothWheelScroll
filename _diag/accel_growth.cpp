// How much does the spit MULTIPLY travel? The spit grows the in-flight pool, and the next frame
// spits on the grown pool -- so the gain is not "Y * X" but an exponential.
// Steady state for a feedback of rate r over a window X: pool ~ feedRate*X/(1 - r*X).
#include <cstdio>
#include <cmath>
#include <initializer_list>
int main(){
  const double X = 100.0;                 // windowMs
  printf("window X = %.0f ms; the spit multiplies itself once per window (r*X >= 1 runs away)\n\n", X);
  printf("  %-8s %-14s %-14s\n","spit r","r*X","steady pool / feed");
  for (double r : {0.5, 2.0, 5.0, 8.0, 9.0}) {
    const double g = r * X / 1000.0;
    printf("  %-8.2f %-14.2f ", r, g);
    if (g < 0.999) printf("%.2fx  (bounded)\n", 1.0/(1.0-g));
    else printf("RUNAWAY (>= 1: grows without bound)\n");
  }
  printf("\n  For a 6-notch flick at 25 ms/notch = 720 deltas in:\n");
  const double in = 720, Xms = 100.0, gap = 25, T = 6*gap;
  for (double r : {2.0, 5.0}) {
    const double perMs = r;
    const double extra = perMs * T * (1.0/(1.0 - perMs*Xms/1000.0));  // gross overshoot estimate
    printf("    spit %.0f/ms -> out ~%.0f deltas (%.1fx of what went in)\n", r, in+extra, (in+extra)/in);
  }
  return 0;
}
