// from x509-parser
// https://github.com/ANSSI-FR/x509-parser/blob/6f3bae3c52989180df6af46da1acb0329315b82a/src/x509-common.c#L7231-L7279

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

/*
 * Max allowed buffer size for ASN.1 structures. Also note that
 * the type used for length in the whole code is an u32, so it
 * is pointless to set something higher than 2^32 - 1
 */
#define MAX_UINT32 (0xffffffffUL)
#define ASN1_MAX_BUFFER_SIZE (MAX_UINT32)

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

/*
 * Handles expected lack of optinal parameters associated with sig and pubkey
 * OID. The function also support the case where lack of parames has been
 * implemented by some software by a adding a NULL instead of nothing.
 * We define specific sig and pubky function from that one below.
 */
/*@
  @ requires ((u64)off + (u64)len) <= MAX_UINT32;
  @ requires ((len > 0) && (cert != \null)) ==> \valid_read(cert + (off .. (off + len - 1)));
  @
  @ ensures \result < 0 || \result == 0;
  @ ensures (cert == \null) ==> \result < 0;
  @ ensures (off == 0 && len == 2 && cert != NULL) ==> (\result == 0 <==> bmatch(cert, &null_encoded_val[0], len));
  @
  @ assigns \nothing;
  @*/
int parse_algoid_params_none(const u8 *cert, u32 off, u32 len)
{
	const u8 *buf = cert + off;
	u32 parsed = 0;
	int ret;

	if (cert == NULL) {
		ret = -X509_FILE_LINE_NUM_ERR;
		//ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		goto out;
	}

	/*@ assert (len > 0) ==> \valid_read(buf + (0 .. len - 1)); */

	switch (len) {
	case 0: /* Nice ! */
		ret = 0;
		break;
	case 2: /* Null ? */
		ret = parse_null(buf, len, &parsed);
		if (ret) {
			//ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		}
		break;
	default: /* Crap ! */
		ret = -1;
		//ERROR_TRACE_APPEND(X509_FILE_LINE_NUM_ERR);
		break;
	}

out:
	return ret;
}


int main() {
	u8 buf[2] = { 0x05, 0x00 };
	u32 len = 2;
	u32 off = 0;

	int ret = parse_algoid_params_none(buf, off, len);
	//@ assert ret == 0;

	return ret;
}