////////////////////////////////////////////////////////////////////////////////////////////////////
// \file arena_allocator.cpp : implementation of arena allocation schema
//
// Afftar: Reuven Bass - inspired by Mamasha Knows
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "arena_allocator.h"



////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////


namespace unitest {

/**/
void arena_simple_usage()
{
	struct SomeSt {
		double d;
		int    i;

		SomeSt()				{;}
		SomeSt(double, int)		{;}
	};

	Arena arena;
	SomeSt* p1   = new (arena) SomeSt[20];

	arena.DTOR(p1, 20);

	SomeSt* p4 = new (arena) SomeSt[10];
	SomeSt* p5 = new (arena) SomeSt(10., 30);

	for (int i = 10000; i-- > 0; )
		arena.DTOR(new (arena) SomeSt);

	arena.DTOR(p4, 10);
	arena.DTOR(p5);
}


/**/
void my_arena_usage()
{
	struct SomeSt {
		double d;
		int    i;

		SomeSt()				{;}
		SomeSt(double, int)		{;}
		~SomeSt()				{ printf("SomeSt::DTOR() %d\n", i); }
	};

	MyArena<256> arena;

	SomeSt* p4 = new (arena) SomeSt[3];
	SomeSt* p5 = new (arena) SomeSt(10., 30);

	p4[0].i = 10;
	p4[1].i = 20;
	p4[2].i = 30;

	p5->i = 500;

	arena.DTOR(p4, 3);
	arena.DTOR(p5);

	arena.clear();

	p4 = new (arena) SomeSt[3];
	p5 = new (arena) SomeSt(10., 30);

	p4[0].i = 10;
	p4[1].i = 20;
	p4[2].i = 30;

	p5->i = 500;

	arena.DTOR(p4, 3);
	arena.DTOR(p5);
}

using namespace boost::intrusive;

class MyStruct
	: public slist_base_hook<>
{
public:
	int indx_ = 0;
};

typedef slist<MyStruct> AList;

/**/
void slist_usecase()
{
	AList l1;
	AList l2;
	
	MyStruct* p = new MyStruct[3];

	p[0].indx_ = 0;
	p[1].indx_ = 1;
	p[2].indx_ = 2;

//	l1.push_back(p[0]);
//	l1.push_back(p[1]);
//	l1.push_back(p[2]);

	l2.push_front(p[0]);
	l2.push_front(p[1]);
	l2.push_front(p[2]);

	for ( AList::iterator it(l2.begin()), end(l2.end()); it != end; ++it )
		printf("%d\n", it->indx_);

	return;
}


/**/
void arena_usecases()
{
	arena_simple_usage();
	my_arena_usage();
	return;
}

}	// namespace unitest

