int main(int argc, char *argv[])
{
	(void)argv;
	__typeof__(argc) i;
	i = argc;
	return &i == &argc ? 0 : 1;
}
