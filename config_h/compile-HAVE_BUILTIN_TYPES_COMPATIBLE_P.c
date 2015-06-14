int main(void)
{
	return __builtin_types_compatible_p(char *, int) ? 1 : 0;
}
