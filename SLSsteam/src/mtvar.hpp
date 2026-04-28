#include <mutex>
#include <shared_mutex>


template<typename T> class MTVariable
{
private:
	T instance;
	std::shared_mutex mutex;

public:
	MTVariable() : instance(empty()) { }
	MTVariable(T instance) : instance(instance) { }

	//Returns default instance
	T empty()
	{
		return T();
	}

	T get()
	{
		const auto lock = std::shared_lock(mutex);
		//Return copy
		return T(instance);
	}

	void set(T value)
	{
		const auto lock = std::unique_lock(mutex);
		instance = value;
	}

	void operator=(MTVariable<T> other)
	{
		set(other.instance);
	}
};
