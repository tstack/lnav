/**
 * @file strong_int.hh
 */

#ifndef __strong_int_hh
#define __strong_int_hh

/**
 * Template class for "strongly-typed" integers, in other words, integers that
 * have different semantic meaning and cannot be easily used in place of one
 * another.
 *
 * @param T The integer type.
 * @param DISTINCT An class used solely to distinguish templates that have the
 * same integer type.
 */
template<typename T, class DISTINCT>
class strong_int {
public:
  explicit strong_int(T v = 0) : value(v) { };
  operator const T & () const { return this->value; };
  strong_int operator+(const strong_int &rhs) {
    return strong_int(this->value + rhs.value);
  };
  strong_int operator-(const strong_int &rhs) {
    return strong_int(this->value - rhs.value);
  };
  strong_int operator/(const strong_int &rhs) {
    return strong_int(this->value / rhs.value);
  };
  strong_int &operator+=(const strong_int &rhs) {
    this->value += rhs.value;
    return *this;
  };
  strong_int &operator-=(const strong_int &rhs) {
    this->value -= rhs.value;
    return *this;
  };
  strong_int &operator-(void) {
    this->value = -this->value;
    return *this;
  };
  strong_int &operator++(void) { this->value++; return *this; };
  strong_int &operator--(void) { this->value--; return *this; };
  T *out(void) { return &this->value; };
private:
  T value;
};

/**
 * Macro that declares a strongly-typed integer and the empty class used as a
 * distinguisher.
 *
 * @param T The integer type.
 * @param name The name of the strongly-typed integer.
 */
#define STRONG_INT_TYPE(T, name) \
  class __##name##_distinct; \
  typedef strong_int<int, __##name##_distinct> name##_t

#endif
