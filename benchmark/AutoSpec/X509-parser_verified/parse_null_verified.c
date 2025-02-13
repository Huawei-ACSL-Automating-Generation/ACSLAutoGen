// from x509-parser
// https://github.com/ANSSI-FR/x509-parser/blob/6f3bae3c52989180df6af46da1acb0329315b82a/src/x509-common.c#L1112-L1156
// https://github.com/ANSSI-FR/x509-parser/blob/6f3bae3c52989180df6af46da1acb0329315b82a/src/x509-utils.c#L15C4-L48

#include <stdint.h>
#include <unistd.h>
#include <string.h>

typedef uint8_t	  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define X509_FILE_NUM 0 /* See x509-utils.h for rationale */

/*
 * Historically, we used -__LINE__ as return value. This worked well when
 * the parser was a single file. Now that we have multiple files in the
 * project, we encode a unique numerical identifier for each file in the
 * return value. For that to work, we need each *implementation* file
 * to define a unique value for X509_FILE_NUM at its beginning.
 */
#define X509_FILE_LINE_NUM_ERR ((X509_FILE_NUM * 100000) + __LINE__)

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

static const u8 null_encoded_val[] = { 0x05, 0x00 };

/*
 * Implements a function for parsing ASN1. NULL object. On success, the function
 * returns 0 and set 'parsed' parameters to the amount of bytes parsed (i.e. 2).
 * -1 is returned on error.
 */

/*@
  @ requires ((len > 0) && (buf != \null)) ==> \valid_read(buf + (0 .. (len - 1)));
  @ requires (parsed != NULL) ==> \valid(parsed);
  @ requires \separated(parsed, buf+(..));
  @
  @ ensures \result < 0 || \result == 0;
  @ ensures (len == sizeof(null_encoded_val) && len != 0 && buf != NULL && parsed != NULL) ==> (\result == 0 <==> bmatch(buf, &null_encoded_val[0], len));
  @ ensures (\result == 0) ==> *parsed == 2;
  @ ensures (buf == \null) ==> \result < 0;
  @
  @ assigns *parsed;
  @*/
int parse_null(const u8 *buf, u32 len, u32 *parsed)
{
	int ret;

	if ((len == 0) || (buf == NULL) || (parsed == NULL)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		//ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	if (len != sizeof(null_encoded_val)) {
		ret = -X509_FILE_LINE_NUM_ERR;
		//ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = bufs_differ(buf, null_encoded_val, sizeof(null_encoded_val));
	if (ret) {
		ret = -X509_FILE_LINE_NUM_ERR;
		//ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	ret = 0;
	*parsed = sizeof(null_encoded_val);

out:
	return ret;
}

int main() {
  const u8 b[] = { 0x05, 0x00 };
  u32 parsed;

  int ret = parse_null(b, sizeof(null_encoded_val), &parsed);
  //@ assert parsed == sizeof(null_encoded_val);
  //@ assert ret == 0;
  
  return 0;
}