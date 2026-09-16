// WHAT EXACTLY does the current Shape do? Print the curve literally, so the definition can be
// checked against intent rather than described in prose.
//
//   g++ -std=c++17 -O2 -I../src -o shape_def.cpp shape_def.cpp

#include "anim3_core.h"
#include <cstdio>
#include <initializer_list>

using namespace anim3;

int main() {
  const double D = 300.0; // ms, a window
  printf("A window lasts %.0f ms. u = how far through the window (0 = start, 1 = end).\n", D);
  printf("S(u) = how much of THIS MESSAGE's notch has been handed over by time u.\n");
  printf("rate = how fast it is being handed over at that instant (that is what the eye sees).\n\n");

  for (double bend : {0.0, 0.5, 1.0}) {
    const double p = 1.0 + 3.0 * bend;
    printf("===== bend = %.1f  (p = %.2f, peak rate = %.2f x average) =====\n", bend, p, p);
    printf("  %-14s %-10s %-12s %s\n", "time in window", "u", "handed by then", "rate now");
    for (int i = 0; i <= 10; ++i) {
      const double u = i / 10.0;
      const double S = ShapeFrac(kSymS, bend, u);
      // numerical rate at u (units of "average rate")
      const double du = 1e-5;
      const double S2 = ShapeFrac(kSymS, bend, (u + du > 1 ? 1.0 : u + du));
      const double rate = (u + du > 1) ? (ShapeFrac(kSymS, bend, 1.0) - ShapeFrac(kSymS, bend, 1.0 - du)) / du
                                       : (S2 - S) / du;
      printf("  %5.0f ms (%-4.0f%%) %-10.2f %-12.1f%% %.2fx\n",
             u * D, u * 100, u, S * 100, rate);
    }
    printf("\n");
  }
  return 0;
}
