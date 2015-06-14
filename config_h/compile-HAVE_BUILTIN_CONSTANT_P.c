int main(void)
{
	return __builtin_constant_p(1) ? 0 : 1;
}
