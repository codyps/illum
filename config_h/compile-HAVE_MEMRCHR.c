#include <string.h>
int main(void)
{
	return memrchr("a\0b", 'a', 3) != NULL;
}
