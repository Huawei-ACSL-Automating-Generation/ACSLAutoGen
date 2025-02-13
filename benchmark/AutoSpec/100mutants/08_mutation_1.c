#include <assert.h>
int unknown1();
int unknown2();
int unknown3();

/*
 *  Based on "Automatically refining abstract interpretations" fig.1
 */


void main() {
 int a = 0, y = 0;
 // invariant (a < 4 || y >= 1)
 while(unknown1()) {
   if(unknown2()) { 
      a++; 
      y += 100; 
   }
   else if (unknown3()){ 
      if (a >= 4) { 
          a++; 
          y++; 
      } 
      if (a < 0) {
          y--;
      }
   }
  
 }
 static_assert(a < 4 || y > 2);
}
