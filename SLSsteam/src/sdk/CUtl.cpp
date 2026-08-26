#include "CUtl.hpp"


CUtlString::~CUtlString()
{
	if (str)
	{
		Steam::Plat_Free(str);
	}
}

bool CUtlString::resize(size_t newSize)
{
	void* mem;

	//Steam allocates size + \0
	if (str)
	{
		mem = Steam::Plat_Realloc(str, newSize + 1);
	}
	else
	{
		mem = Steam::Plat_Alloc(newSize + 1);
	}

	if (!mem)
	{
		return false;
	}

	str = reinterpret_cast<char*>(mem);
	return true;
}

void CUtlString::setValue(const char* newStr)
{
	//Steam switches pointers then frees the old one
	//But that's not what we gonna do

	const size_t len = strlen(newStr);
	if (!resize(len))
	{
		return;
	}

	memcpy(str, newStr, len);
	str[len] = '\0'; //Not out of bounds, realloc does + 1 for \0
}
