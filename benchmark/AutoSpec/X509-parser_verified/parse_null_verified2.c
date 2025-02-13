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
requires n >= 0;
requires \valid(b2+(0..n-1));
requires \valid(b1+(0..n-1));
ensures \result == 0 ==> \result == 0;
ensures \result == 0 ==> \result == -X509_FILE_LINE_NUM_ERR || \result == 0;
ensures \result == -X509_FILE_LINE_NUM_ERR ==> \result == -X509_FILE_NUM * 100000 + __LINE__;
ensures \result == -X509_FILE_LINE_NUM_ERR ==> \result == -100000;
ensures esult == 1 <==> \exists integer i; 0 <= i < n && b1[i] != b2[i];
*/
int bufs_differ(const u8 *b1, const u8 *b2, u32 n)
{
	int ret = 0;
	u32 i = 0;

	/*@
	loop invariant ret == 0 || ret == -X509_FILE_LINE_NUM_ERR;
	loop invariant ret == 0 || ret == -100000;
	loop invariant ret == 0 ==> \forall integer k; 0 <= k < i ==> b1[k] == b2[k];
	loop invariant i <= n;
	loop invariant \forall integer k; 0 <= k < i ==> b1[k] == b2[k];
	loop invariant \forall int k; 0 <= k < i ==> b1[k] == b2[k];
	loop invariant 0 <= i;
	loop assigns n;
	loop assigns i;
	*/
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
requires len >= 0;
requires len > 0;
requires len == sizeof(null_encoded_val);
requires len == 0 || \valid(buf+(0..len-1));
requires len != sizeof(null_encoded_val);
requires \valid(parsed);
requires \valid(buf+(0..len-1));
requires \separated(buf, parsed);
ensures parsed != NULL ==> *parsed == 2;
ensures \result == 0;
ensures \result == 0 ==> \result == 0;
ensures \result == 0 ==> \result == -X509_FILE_LINE_NUM_ERR;
ensures \result == 0 ==> \result == -X509_FILE_LINE_NUM_ERR || \result == 0;
ensures \result == 0 ==> *parsed == sizeof(null_encoded_val);
ensures \result == 0 ==> *parsed == 2;
ensures \result == 0 && len == sizeof(null_encoded_val) ==> *parsed == sizeof(null_encoded_val);
ensures \result == 0 && len != sizeof(null_encoded_val) ==> *parsed == \old(*parsed);
ensures \result == -X509_FILE_LINE_NUM_ERR;
ensures \result == -X509_FILE_LINE_NUM_ERR ==> \result == -X509_FILE_NUM * 100000 + __LINE__;
ensures \result == -X509_FILE_LINE_NUM_ERR ==> \result == -100000;
ensures \result == -X509_FILE_LINE_NUM_ERR ==> *parsed == sizeof(null_encoded_val);
ensures \result == -X509_FILE_LINE_NUM_ERR <==> \exists integer i; 0 <= i < len && i < 2 && buf[i] != null_encoded_val[i];
ensures \result == -1 ==> \result == -X509_FILE_LINE_NUM_ERR;
ensures \result != 0 ==> *parsed == \old(*parsed);
ensures \result != -X509_FILE_LINE_NUM_ERR ==> \result == -X509_FILE_LINE_NUM_ERR;
ensures \result != -X509_FILE_LINE_NUM_ERR <==> \exists integer i; 0 <= i < len && i < 2 && buf[i] != null_encoded_val[i];
ensures *parsed == sizeof(null_encoded_val);
ensures *parsed == \old(*parsed);
ensures esult == 1 <==> \exists integer i; 0 <= i < n && b1[i] != b2[i];
*/
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

  int ret = parse_null(b, 2, &parsed);
  //@ assert parsed == 2;
  //@ assert ret == 0;
  
  return 0;
}