////////////////////////////////////////////////////////////////////////////////////////////////////
// \file arena_allocator.h : arena allocation schema
//
// Afftar: Reuven Bass - inspired by Mamasha Knows
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <boost/intrusive/slist.hpp>

//! one direction linked list; base item
typedef boost::intrusive::slist_base_hook<> slist_item;


//! arena allocator (with registered DTORs)
class Arena
{
	//! implementation of destructor callback for a given type
	template<class T>
	struct DtorFn
	{
		static void impl(void* self, int count)
		{
			// go through destructors in reverse order
			T* it = ((T*) self) + count - 1;		// last element
			for ( int i = count; i-- > 0; --it )
				it->~T();
		}
	};

	// pointer to implementation of dtor function
	typedef void (*dtor_fn_ptr) (void*, int);

	//! dtor record describes destructor(s) to be called
	struct DtorRec : slist_item
	{
		void*			ptr_;		//!< pointer to object(s)
		dtor_fn_ptr		dtor_;		//!< pointer to dtor implementation		
		int				count_;		//!< number of elements to destroy

		//! construct and initialize record
		inline DtorRec(void* ptr, int count)
			: ptr_(ptr), count_(count), dtor_(NULL)
		{;}

		//! invoke dtor
		inline void dtor()		{ dtor_(ptr_, count_); }
	};

	//! memory block descriptor
	struct MemBlock : slist_item
	{
		size_t	next_;		//!< next allocation goes here
		size_t	size_;		//!< block size

		// construct and initialize
		inline MemBlock(size_t size) : size_(size)
		{
			reset();
		}

		//! get current position; advance to next allocation
		inline char* alloc(size_t size)
		{
			BOOST_ASSERT( has_room_of(size) );
			
			// get allocation place
			char* ptr = next();

			// advance to next allocation
			next_ += size;

			// align next allocation
			if ( int rmndr = size % ALIGNMENT )
				next_ += ALIGNMENT - rmndr;

			return ptr;
		}

		//! true if has room of a specified size
		inline bool has_room_of(size_t size) const
		{
			return
				next_ + size <= size_;
		}

		//! reset memory block
		inline void reset()						{ next_ = sizeof(*this); }

		//! get next as pointer to memory
		inline char* next()						{ return ((char*) this) + next_; }
	};

	//! one direction linked list of DTOR records
	typedef boost::intrusive::slist<DtorRec>	DtorList;
	typedef DtorList::iterator					DtorIt;

	//! one direction linked list of allocated blocks
	typedef boost::intrusive::slist<MemBlock>	BlockList;
	typedef BlockList::iterator					BlockIt;

public:

	// defaults
	enum { BLOCK_SIZE = 30 * 1024 - sizeof(MemBlock) };
	enum { MAX_SMALL_ALLOC_SIZE = BLOCK_SIZE / 3 };
	enum { ALIGNMENT = 4 };

	BOOST_STATIC_ASSERT(0 == sizeof(MemBlock) % ALIGNMENT);

	//! create arena and initialize
	inline Arena()
		: cur_(NULL), initial_(NULL)
	{;}


	//! create arena with initial storage
	Arena(void* ptr, int initCapacity)
		: cur_(NULL), initial_(NULL)
	{
		BOOST_ASSERT(initCapacity >= sizeof(MemBlock) );

		// initialize current block
		initial_ = new (ptr) MemBlock(initCapacity);
		cur_     = initial_;
	}


	//! free all memory blocks
	inline virtual ~Arena()
	{
		// destroy objects
		invoke_dtors();

		// free allocations
		free_blocks();
	}


	//! allocate storage
	void* alloc(size_t size)
	{
		void* ptr = NULL;

		// break when allocated
		do 
		{
			// if bigger then max small size allocate from heap
			if ( size > MAX_SMALL_ALLOC_SIZE )
			{
				// allocate block of required size
				MemBlock* block = alloc_block(size);

				// use all memory
				ptr = block->alloc(size);

				// store newly allocated block
				blocks_.push_front(*block);

				break;
			}

			// if no current block allocate one
			if ( cur_ == NULL )
				cur_ = alloc_block(BLOCK_SIZE);

			// if no space in current block allocate a new one
			if ( ! cur_->has_room_of(size) )
			{
				// store current to list of blocks
				if ( cur_ != initial_ )
					blocks_.push_front(*cur_);

				// get new one
				cur_ = alloc_block(BLOCK_SIZE);
			}

			// allocate memory
			ptr = cur_->alloc(size);
		}
		while (false);

		// update statistics
        statistics_.num_of_allocs_++;
        statistics_.bytes_allocated_ += size;

		return ptr;
	}


	//! register DTOR for an object (array of objects)
	//! DTOR will be called when arena is destroyed (cleared)
	template<typename T>
	void DTOR(T* ptr, int count = 1)
	{
		// allocate and initialize DTOR record
		DtorRec* rec = new (*this) DtorRec(ptr, count);

		// initialize pointer to dtor implementation function
		rec->dtor_ = &DtorFn<T>::impl;

		// store to list of dtors - reverse order
		dtors_.push_front(*rec);

		// update statistics
		statistics_.num_of_dtros_++;
	}


	//! call registered DTORs, then free all memory blocks
	void clear()
	{
		// destroy all objects
		invoke_dtors();

		// free all allocated blocks
		free_blocks();

		// reset
		cur_ = initial_;

		if ( cur_ != NULL )
			cur_->reset();
	}

protected:

	//! allocate memory block
	MemBlock* alloc_block(size_t size)
	{
		// add block size to required size
		size += sizeof(MemBlock);

		// allocate from heap
		char* buf = new char [size];
		BOOST_ASSERT(buf);

		// construct MemBlock on top of newly allocated memory
		MemBlock* block = new (buf) MemBlock(size);

		// update statistics
		statistics_.num_of_blocks_++;

		return block;
	}

	//! invoke registered destructors
	void invoke_dtors()
	{
		// invoke each registered record
		for ( DtorIt it = dtors_.begin(); it != dtors_.end(); ++it )
			it->dtor();

		// clear list
		dtors_.clear();
	}

	//! free memory blocks
	void free_blocks()
	{
		// disposer of a block
		struct { void operator () (MemBlock* block)
		{
			// destroy block (clear slist reference)
			block->~MemBlock();

			// free memory
			delete [] (char*) block;
		}}
		disposer;

		// free all blocks
		blocks_.clear_and_dispose(disposer);

		// free current block
		if ( cur_ != initial_ )
			disposer(cur_);

		cur_ = NULL;
	}

	// data members
	MemBlock*	cur_;				//!< current memory block
	MemBlock*   initial_;			//!< initial block (not allocated here)
	DtorList	dtors_;				//!< list of registered DTORs
	BlockList	blocks_;			//!< list of allocated blocks
 
public:
    
	struct Statistics {
	    int			num_of_allocs_   = 0;	//!< total number of allocations
	    int			num_of_blocks_   = 0;	//!< total number of blocks in use
	    int			num_of_dtros_    = 0;	//!< total number of dtor records
        size_t      bytes_allocated_ = 0;	//!< total number of allocated bytes
    } statistics_;
};


//! in-place (stack) arena allocator
template<int INIT_CAPACITY>
class MyArena : public Arena
{
public:

	//! construct and initialize
	MyArena()
		: Arena(storage_, INIT_CAPACITY)
	{;}

protected:

	char	storage_[INIT_CAPACITY];
};



namespace unitest {
	void arena_usecases();
}




// new operators (should be global)

inline
	void* operator new (size_t size, Arena& arena)
{
	return
		arena.alloc(size);
}

template <typename Type, Type* P, typename = typename std::enable_if<(P != nullptr)>::type> inline
    void* operator new (size_t size, Arena* arena)
{
	return
		arena->alloc(size);
}


// delete operators (should be global)

inline
	void operator delete (void* ptr, Arena& alctr)
{(void)ptr;(void)alctr;}


inline
	void operator delete (void* ptr, Arena* alctr)
{(void)ptr;(void)alctr;}



