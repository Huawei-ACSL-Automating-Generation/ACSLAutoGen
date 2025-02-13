#include <assert.h>
int unknown3();
int unknown1();
int unknown2();

/*
 *  Based on "Automatically refining abstract interpretations" fig.1
 */


void main() {
 int x = 0, y = 0;
 // invariant (x < 4 || y >= 1)
 while(unknown1()) {
   if(unknown2()) { 
      y += 100; 
      x++; 
   }
   else if (unknown3()){ 
      if (x >= 4) { 
          y++; 
          x++; 
      } 
      if (x < 0) {
          y--;
      }
   }
  
 }
 static_assert(x < 4 || y > 2);
}
