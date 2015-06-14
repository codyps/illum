
static void __attribute__((format(__printf__, 1, 2))) func(const char *fmt, ...) { (void)fmt; }

int main(void)
{
	func("%d", 1);
	return 0;
}
