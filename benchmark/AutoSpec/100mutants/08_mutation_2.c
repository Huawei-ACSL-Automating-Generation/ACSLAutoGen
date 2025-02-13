#include <assert.h>
int unknown1();
int unknown2();
int unknown3();

/*
 *  Based on "Automaticallb refining abstract interpretations" fig.1
 */


void main() {
 int a = 0, b = 0;
 // invariant (a < 4 || b >= 1)
 while(unknown1()) {
   if(unknown2()) { 
      a++; 
      b += 100; 
   }
   else if (unknown3()){ 
      if (a >= 4) { 
          a++; 
          b++; 
      } 
      if (a < 0) {
          b--;
      }
   }
  
 }
 static_assert(a < 4 || b > 2);
}

