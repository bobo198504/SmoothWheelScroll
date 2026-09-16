// A SLOW roll whose gaps JITTER (as a real hand's do). Does any message flip to "spit"?
//
//   the decision (mirrors Kick):  speed = 120 / gapMs ;  spit if speed > kEatStopSpeed (2.4)
// so the flip happens on any gap shorter than 50 ms. A slow roll mostly has gaps of 100-300 ms,
// but if ONE gap comes in under 50 ms, that one message flips -- and since eat is *0.1 while spit
// is *1.1, that single message is ~11x the others.
#include <cstdio>
#include <cmath>
#include <initializer_list>
static const double kEatStop = 120.0/50.0;
int main(){
  printf("kEatStopSpeed = %.2f deltas/ms  ->  a message flips to SPIT when its gap < %.0f ms\n\n",
         kEatStop, 120.0/kEatStop);
  // A hand rolling slowly but unevenly: gaps in ms.
  const double hand[] = {220, 180, 240, 150, 210, 90, 260, 45, 230, 200, 170, 40, 210, 190};
  printf("  %-6s %-8s %-8s %-10s %-12s\n","#","gap ms","speed","decision","scale");
  for (int i=0;i<14;++i){
    const double speed = 120.0/hand[i];
    const bool spit = speed > kEatStop;
    const double eatFrac = 0.90;           // the slider at max
    const double scale = spit ? 1.1 : (1.0-eatFrac);
    printf("  %-6d %-8.0f %-8.2f %-10s %-12.3f%s\n", i+1, hand[i], speed, spit?"SPIT":"eat", scale,
           (i>0 && spit) ? "   <-- this one jumps" : "");
  }
  printf("\n  So: on an uneven slow roll, the messages whose gap happens to fall under 50 ms\n");
  printf("  are multiplied by 1.1 while their neighbours are multiplied by 0.1 -- an 11x step\n");
  printf("  on a single message. That reads as 'slow, then suddenly fast'.\n");
  return 0;
}
