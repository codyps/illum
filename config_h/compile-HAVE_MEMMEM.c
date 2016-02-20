#include <string.h>
int main(void)
{
	return memmem("ab", 2, "b", 1) == NULL;
}
