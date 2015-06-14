#include <stdlib.h>
static void
__attribute__((__noreturn__))
func(int x)
{
	exit(x);
}

int main(void)
{
	func(1);
}
