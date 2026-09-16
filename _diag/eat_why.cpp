// WHY does the eaten fraction saturate below Y?
//
// One message (amount A) in a window of X ms. The delivery pays at `a / time-left` each frame, so it
// plans to finish whatever is still there by the end of the window; the brake takes a fixed r per ms.
//   da/dt = -a/(X-t) - r      with a(0)=A
// Let s = X-t. Then  da/ds = a/s + r,  so  a/s = A/X - r*ln(X/s),  i.e.
//   a(s) = s*(A/X - r*ln(X/s))
// The window dies where a hits 0:  s0 = X*exp(-A/(rX)).  The brake ran for X - s0, so
//   eaten = r*(X - s0) = rX*(1 - exp(-A/(rX))).
// With the plugin's rate r = (A/X)*y  (y = Y/100),  A/(rX) = 1/y, so
//   EATEN FRACTION = y * (1 - e^(-1/y))
#include <cstdio>
#include <cmath>
#include <initializer_list>
static double ClosedForm(double y){ return (y<=0)?0.0:y*(1.0-std::exp(-1.0/y)); }
int main(){
  printf("eaten fraction = y*(1 - e^(-1/y)),  y = Y/100 (the slider as a fraction)\n\n");
  printf("  %-8s %-10s %s\n","Y %","eaten %","");
  for(double Y : {10.0,30.0,50.0,70.0,80.0,90.0}){
    const double e = ClosedForm(Y/100.0);
    printf("  %-8.0f %-10.2f%s\n",Y,100*e, Y==90.0?"   <- the slider at 90% only eats ~60%":"");
  }
  printf("\n  the CEILING as the rate grows:  lim  y*(1-e^(-1/y)) = 1  (but only as y -> inf)\n");
  printf("  %-8s %-10s\n","y (rate x)","eaten %");
  for(double y : {1.0,2.0,4.0,8.0,16.0,64.0}) printf("  %-8.0f %-10.2f\n",y,100*ClosedForm(y));

  printf("\n  WHY: the delivery is proportional to what is left, so it always finishes the window;\n");
  printf("  the brake is a fixed rate, so the window's AMOUNT reaches zero before the window's TIME\n");
  printf("  does. Once a = 0 the window is gone and there is nothing left to eat. At y = 1 (Y = 100%%)\n");
  printf("  the window dies at s0 = X*e^-1 = 0.368X, i.e. 63%% of the way through, so 63%% is the most\n");
  printf("  a rate of A/X can ever eat.\n");

  printf("\n  WHAT RATE WOULD EAT 90%%:  solve y*(1-e^(-1/y)) = 0.90\n");
  double lo=1.0,hi=20.0;
  for(int i=0;i<80;++i){ const double m=0.5*(lo+hi); if(ClosedForm(m)<0.90) lo=m; else hi=m; }
  printf("    y = %.3f  ->  rate = (A/X) * %.3f  (times the message's own delivery rate)\n",0.5*(lo+hi),0.5*(lo+hi));
  printf("    at Y=90%% the plugin currently uses 0.90 -- %.2fx too small.\n",0.5*(lo+hi)/0.9);
  return 0;
}
