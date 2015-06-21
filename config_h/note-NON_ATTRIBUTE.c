/* FAILED: compiler does not emit an error for unrecognized attributes, config.h definitions may be incorrect */
/* SUCCEEDED: compiler errors when unrecognized attributes are used, no effect on config.h accuracy */
__attribute__((some_fake_attribute))
int main(void)
{
	return 0;
}
