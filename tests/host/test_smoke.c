#include "t.h"

static void test_arithmetic(void) { T_EQ_INT(1 + 1, 2); }

int main(void)
{
	T_RUN(test_arithmetic);
	return T_DONE();
}
