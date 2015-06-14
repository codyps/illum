static __attribute__((__warn_unused_result__)) int func(int i)
{
	return i + 1;
}

int main(int argc, char **argv)
{
	(void)argv;
	return func(argc);
}
