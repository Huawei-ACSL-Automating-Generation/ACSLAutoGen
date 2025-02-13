// from x509-parser
// https://github.com/ANSSI-FR/x509-parser/blob/6f3bae3c52989180df6af46da1acb0329315b82a/src/x509-utils.c#L15C4-L48

/*
 *  Copyright (C) 2022 - This file is part of x509-parser project
 *
 *  Author:
 *      Arnaud EBALARD <arnaud.ebalard@ssi.gouv.fr>
 *
 *  This software is licensed under a dual GPLv2/BSD license. See
 *  LICENSE file at the root folder of the project.
 */

#include <stdint.h>
#include <unistd.h>
#include <string.h>

typedef uint8_t	  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define X509_FILE_NUM 0 /* See x509-utils.h for rationale */

/*@
  @ predicate bmatch(u8 *b1, u8 *b2, u32 n) =
  @   \forall integer i; 0 <= i < n ==> b1[i] == b2[i];
  @
  @ predicate bdiffer(u8 *b1, u8 *b2, u32 n) =
  @   ! bmatch(b1, b2, n);
  @*/
/*@
  @
  @ requires \valid_read(b1 + (0 .. n-1));
  @ requires \valid_read(b2 + (0 .. n-1));
  @ requires \valid(b1 + (0 .. n-1));
  @ requires \valid(b2 + (0 .. n-1));
  @ requires n >= 0;
  @ ensures \result == 0 <==> bmatch(b1, b2, n);
  @ ensures \result == 1 <==> bdiffer(b1, b2, n);
  @
  @ assigns \nothing;
  @*/
int bufs_differ(const u8 *b1, const u8 *b2, u32 n)
{
	int ret = 0;
	u32 i = 0;

	/*@
	  @ loop invariant 0 <= i <= n;
	  @ loop invariant bmatch(b1, b2, i);
	  @ loop assigns i;
	  @ loop variant n - i;
	  @*/
	for (i = 0; i < n; i++) {
		if(b1[i] != b2[i]) {
			ret = 1;
			break;
		}
	}

	return ret;
}


int main() {
  u8 b1[10] = {1,2,3,4,5,6,7,8,9,10};
  u8 b2[10] = {10,9,8,7,6,5,4,3,2,1};
  u8 b3[10] = {1,2,3,4,5,6,7,8,9,10};
  u32 n = 10;

  int ret1 = bufs_differ(b1, b3, n);
  //@ assert ret1 == 0;

  int ret2 = bufs_differ(b1, b2, n);
  //@ assert ret2 == 1;
  
  return 0;
}