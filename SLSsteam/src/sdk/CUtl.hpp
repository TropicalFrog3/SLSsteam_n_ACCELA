#pragma once

#include "types.hpp"
#include "steam.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr static size_t CUTL_DEFAULT_ALLOC = 0x16;

template<typename T>
SDK_Class CUtlMemory
{
public:
	T* base;
	uint32_t alloc;
	uint32_t growSize;

	CUtlMemory()
	{
		base = nullptr;
		alloc = 0;
		growSize = 0;
	}

	~CUtlMemory()
	{
		if (base)
		{
			Steam::Plat_Free(base);
		}
	};

	bool resize(const size_t newSize)
	{
		void* mem;

		if (base)
		{
			mem = Steam::Plat_Realloc(base, newSize);
		}
		else
		{
			mem = Steam::Plat_Alloc(newSize);
		}

		if (!mem)
		{
			return false;
		}

		base = reinterpret_cast<T*>(mem);
		return true;
	}
};

SDK_Class CUtlBuffer
{
	typedef int(*CUtlBuffer_Function1_t)(void*);
	typedef bool(*CUtlBuffer_Resize_t)(void*, int32_t);

public:
	CUtlMemory<uint8_t> mem;			//0x0
	int32_t get;						//0xC
	int32_t put;						//0x10
	int32_t offset;						//0x14
	uint32_t flags;						//0x18
	CUtlBuffer_Function1_t fn1C;		//0x1C
	int32_t field20;					//0x20
	CUtlBuffer_Resize_t resizeFn;		//0x24
	int32_t field28;					//0x28

	bool resize(const size_t newSize);
}; //0x2C


template<typename T, typename T2>
SDK_Class CUtlRBTree
{
public:
	struct Element_t
	{
		int32_t leftIndex;			//0x0
		int32_t rightIndex;			//0x4
		uint8_t __pad0x0[0x8];		//0x8
		T key;						//0x10
		T2* value;					//0x14
	}; //0x18

	uint8_t __pad0x0[0x14];		//0x0
	int32_t rootNodeIndex;		//0x14
	uint32_t allocated;			//0x18
	uint8_t __pad0x1C[0x4];		//0x1C
	uint32_t size;				//0x20
	uint8_t __pad0x24[0x4];		//0x24
	Element_t* elements;		//0x28

	Element_t* find(const T key)
	{
		int32_t index = rootNodeIndex;

		for (;;)
		{
			const auto node = &elements[index];

			if (node->key == key)
			{
				return node;
			}

			if (key > node->key)
			{
				index = node->rightIndex;
			}
			else
			{
				index = node->leftIndex;
			}

			if (index == -1)
			{
				break;
			}
		}

		return nullptr;
	}
};

template<typename T, typename T2>
SDK_Class CUtlMap
{
public:
	CUtlRBTree<T, T2> tree;		//0x0

	T2* at(size_t key)
	{
		const auto node = tree.find(key);
		if (node)
		{
			return node->value;
		}

		return nullptr;
	}
};

//Null terminated wrapper
SDK_Class CUtlString
{
public:
	char* str;

	~CUtlString();

	constexpr const char* get()
	{
		return str;
	}

	bool resize(const size_t newSize);
	void setValue(const char* newStr);
};

template<typename T>
SDK_Class CUtlVector
{
public:
	CUtlMemory<T> mem;
	uint32_t size;

	constexpr T* at(uint32_t index)
	{
		if (index >= size)
		{
			return nullptr;
		}

		return &mem.base[index];
	};

	constexpr bool resize(size_t newSize)
	{
		if (!mem.resize(newSize))
		{
			return false;
		}

		size = newSize;
		return true;
	}

	constexpr bool swap(uint32_t index, uint32_t index2)
	{
		if (index > size || index2 > size)
		{
			return false;
		}

		T buf = *at(index2);
		*at(index2) = *at(index);
		*at(index) = buf;

		return true;
	}
};
