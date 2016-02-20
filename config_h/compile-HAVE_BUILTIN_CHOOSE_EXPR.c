int main(void)
{
	return __builtin_choose_expr(1, 0, 1.0);
}
