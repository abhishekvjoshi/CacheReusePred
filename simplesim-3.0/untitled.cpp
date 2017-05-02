#define SETS_JUMPS 2

struct A
{
	int A1;
	int A2;
	int A3;
	int A4;
};

struct B
{
	signed int x;
	struct A a;
	unsigned int y;

};

struct C
{
	struct B *b;
	unsigned int y;

};

struct C *c;