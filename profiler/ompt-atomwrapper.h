#include <atomic>

// wrappers allowing to treat atomic and non-atomic variables in the same way

template <typename T>
struct noatomwrapper;

template <typename T>
struct atomwrapper;

//typedef noatomwrapper<double> threadDataTime;
typedef noatomwrapper<double> threadDataTime;
typedef atomwrapper<double> globalDataTime;
//typedef noatomwrapper<long long> threadDataItem;
typedef noatomwrapper<long long> threadDataItem;
typedef atomwrapper<long long> globalDataItem;


template <typename value_type>
value_type atomic_add(std::atomic<value_type> &operand,
                      value_type value_to_add) {
  value_type old = operand.load(std::memory_order_consume);
  value_type desired = old + value_to_add;
  while (!operand.compare_exchange_weak(old, desired, std::memory_order_release,
                                        std::memory_order_consume))
    desired = old + value_to_add;

  return desired;
}

template <typename value_type>
value_type atomic_add(value_type &operand, value_type value_to_add) {
  return operand += value_to_add;
}

template <typename T> struct atomwrapper {
  std::atomic<T> _a;

  atomwrapper() : _a(0) {}

  atomwrapper(const std::atomic<T> &a) : _a(a.load()) {}

  atomwrapper(const T &a) : _a(a) {}

  atomwrapper(const atomwrapper &other) : _a(other._a.load()) {}

  atomwrapper &operator=(const atomwrapper &other) {
    _a.store(other._a.load());
  }

  atomwrapper &operator++() { return ++_a; }

  atomwrapper operator++(int) { return _a++; }

  T add(const T &v) { return atomic_add(_a, v); }

  T load() { return _a.load(); }
};

template <typename T> struct noatomwrapper {
  T _a;

  noatomwrapper() : _a(0) {}

  noatomwrapper(const T &a) : _a(a) {}

  noatomwrapper(const noatomwrapper &other) : _a(other._a) {}

  noatomwrapper &operator=(const noatomwrapper &other) { _a = other._a; }

  noatomwrapper &operator=(const T &a) { _a = a; }

  noatomwrapper &operator++() { return ++_a; }

  noatomwrapper operator++(int) { return _a++; }

  T add(const T &v) { return _a += v; }

  T load() { return _a; }
};

