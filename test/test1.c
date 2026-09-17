#include <stdio.h>
#include <string.h>
#include "mixbuffer.h"
#include "helpers.h"

void hex_encode(unsigned char * bytes, unsigned length)
{
	for(; length; --length)
	{
		fprintf(stderr, "%02X", *bytes++);
	}
}

void check_result1(unsigned st, int lbuf, const struct mixbuffer * mixb, struct mixstream * lb)
{

	struct state {
		char			buffer[40];
		struct mixbuffer	mb;
		struct mixstream	st;
	};
	struct state states[50];

	if(st < ITEMS_OF(states))
	{
//		struct state * state = states + st;
/*
		state->rb.buffer = rb->buffer;
		int rv = memcmp(rb->buffer, state->buffer, sizeof(state->buffer)) == 0 
				&&
			memcmp(rb, &state->rb, sizeof(state->rb)) == 0 
				&&
			memcmp(lb, &state->lb, sizeof(state->lb)) == 0;
*/
		fprintf(stderr, "'");
		hex_encode(mixb->rb.buffer, mixb->rb.size);
		fprintf(stderr, "', %2u, %2u, %2u, %2u, %2u, %2u, %2d\n", 
			(unsigned)mixb->rb.size, 
			(unsigned)mixb->rb.used, 
			(unsigned)mixb->rb.read, 
			(unsigned)mixb->rb.write, 
			(unsigned)lb->write, 
			(unsigned)lb->used, 
			lbuf
			);
	}
}

#/* */
void test_suite1()
{
	unsigned i;
	char buffer[40];
	
	static const char x1[] = { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00 };
	static const char x2[] = { 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 };
	static const char x3[] = { 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00 };
	static const char x4[] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 };
//	static const char x5[] = { 0x05, 0x05, 0x05, 0x05, 0x05, 0x00 };
//	static const char x6[] = { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x00 };
//	static const char x7[] = { 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00 };
//	static const char x8[] = { 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x00 };
//	static const char x9[] = { 0x10, 0x10, 0x10, 0x10, 0x00 };
	static const char * strings[] = {
		x1, x2, x3, x4, 
//		x5, x6, x7, x8, x9
		};

	struct mixbuffer mb;
	struct mixstream locals[5];

	memset(buffer, 0, sizeof(buffer));

	mixb_init(&mb, buffer, sizeof(buffer));
	for(i = 0; i < ITEMS_OF(locals); i++)
		mixb_attach(&mb, &locals[i]);

	fprintf(stderr, "Testing rb_overwrite()");

	
	fprintf(stderr, "Data                                                      size used read write write1 used1 idx\n");
	for(i = 0; i < 50; i++) {
		int idx = i % ITEMS_OF(strings);
		unsigned length = strlen(strings[idx]);
		int lbuf = i % ITEMS_OF(locals);

		if(mixb_free(&mb, &locals[lbuf]) < length)
			mixb_read_upd(&mb, length - mixb_free(&mb, &locals[lbuf]));
		
		mixb_write(&mb, &locals[lbuf], strings[idx], length);
		check_result1(i, lbuf, &mb, &locals[lbuf]);
	}

	for(i = 0; i < ITEMS_OF(locals); i++)
		mixb_detach(&mb, &locals[i]);
	
}

#/* */
int test_rb_find_nth_crlf()
{
	/* two-line "+CDS: <len>\r\n<pdu>\r\n" URC, see at_read_result_iov() */
	static const struct test_case {
		const char	* data;
		size_t		start;		/* read position, to test wrapping around the end of the buffer */
		unsigned	n;
		ssize_t		result;
	} cases[] = {
		{ "+CDS: 25\r\nABCDEF\r\n",			 0, 2, 16 },	/* two complete lines */
		{ "+CDS: 25\r\nABCDEF\r\n",			 0, 1,  8 },	/* first terminator */
		{ "+CDS: 25\r\nABCDEF\r\n",			58, 2, 16 },	/* wrapped around the end of the ring */
		{ "+CDS: 25\r\nABC",				 0, 2, -1 },	/* PDU line incomplete */
		{ "+CDS: 25\r\nABCDEF\r",			 0, 2, -1 },	/* only the '\r' of the PDU line arrived */
		{ "+CDS: 25\r\nABCDEF\r\n\r\n+CMTI: \"SM\",0\r\n",	 0, 2, 16 },	/* next URC already buffered, stop at 2nd terminator */
		{ "",						 0, 1, -1 },
	};
	char buffer[64];
	struct ringbuffer rb;
	unsigned idx;
	int faults = 0;

	fprintf(stderr, "Testing rb_find_nth_crlf()\n");
	for(idx = 0; idx < ITEMS_OF(cases); idx++)
	{
		size_t len = strlen(cases[idx].data);
		size_t i;
		ssize_t res;

		memset(buffer, 'X', sizeof(buffer));
		rb_init(&rb, buffer, sizeof(buffer));
		rb.read = rb.write = cases[idx].start;
		for(i = 0; i < len; i++)
			buffer[(cases[idx].start + i) % sizeof(buffer)] = cases[idx].data[i];
		rb.used = len;
		rb.write = (cases[idx].start + len) % sizeof(buffer);

		res = rb_find_nth_crlf(&rb, cases[idx].n);
		fprintf(stderr, "/* %u */ rb_find_nth_crlf(start %2u, n %u) = %3d\t%s\n", idx, (unsigned)cases[idx].start, cases[idx].n, (int)res, res == cases[idx].result ? "OK" : "FAIL");
		if(res != cases[idx].result)
			faults++;
	}
	fprintf(stderr, "\n");
	return faults;
}

#/* */
int main()
{
	test_suite1();
	if (test_rb_find_nth_crlf()) {
		return 1;
	}
	return 0;
}
