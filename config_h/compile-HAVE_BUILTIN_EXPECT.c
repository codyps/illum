int main(int argc, char *argv[])
{
	(void)argv;
	return __builtin_expect(argc == 1, 1) ? 0 : 1;
}
