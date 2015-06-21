/* WARN: compiler does not emit an error for unrecognized attributes, config.h definitions may be incorrect */
__attribute__((some_fake_attribute))
int main(void)
{
	return 0;
}
