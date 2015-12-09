/* Serialization/deserialization of ieee754 floats for compatibility with exotic
 * platforms that don't support it natively. This file is included by mpack.c
 * when NO_NATIVE_IEEE754 is defined.
 *
 * Reference:
 * http://www.rfwireless-world.com/Tutorials/floating-point-tutorial.html */

/* Macro to compute powers of 2 > 31 without requiring 64-bit integers */
#define POW2(n) \
  ((double)(1 << (n / 2)) * (double)(1 << (n / 2)) * (double)(1 << (n % 2)))

static mpack_value_t pack_ieee754(double v, unsigned mantbits, unsigned expbits)
{
  mpack_value_t rv;
  mpack_int32_t exponent, bias = (1 << (expbits - 1)) - 1;
  mpack_uint32_t sign;
  double mant;

  if (v == 0) {
    rv.components.lo = 0;
    rv.components.hi = 0;
    goto end;
  }

  if (v < 0) sign = 1, mant = -v;
  else sign = 0, mant = v;

  exponent = 0;
  while (mant >= 2.0) mant /= 2.0, exponent++;
  while (mant < 1.0 && exponent > -(bias - 1)) mant *= 2.0, exponent--;

  if (mant < 1.0) exponent = -bias; /* subnormal value */
  else mant = mant - 1.0; /* remove leading 1 */
  exponent += bias;
  mant *= POW2(mantbits);

  if (mantbits == 52) {
    rv.components.hi = (mpack_uint32_t)(mant / POW2(32));
    rv.components.lo = (mpack_uint32_t)(mant - rv.components.hi * POW2(32));
    rv.components.hi |= ((mpack_uint32_t)exponent << 20) | (sign << 31);
  } else if (mantbits == 23) {
    rv.components.hi = 0;
    rv.components.lo = (mpack_uint32_t)mant;
    rv.components.lo |= ((mpack_uint32_t)exponent << 23) | (sign << 31);
  } else {
    assert(0);
  }

end:
  return rv;
}

static void process_float_token(mpack_token_t *t)
{
  mpack_uint32_t sign;
  mpack_int32_t exponent, bias;
  unsigned mantbits;
  unsigned expbits;
  double mant;

  if (t->data.value.f64 == 0) return;  /* nothing to do */

  if (t->length == 4) mantbits = 23, expbits = 8;
  else mantbits = 52, expbits = 11;
  bias = (1 << (expbits - 1)) - 1;

  /* restore sign/exponent/mantissa */
  if (mantbits == 52) {
    sign = t->data.value.components.hi >> 31;
    exponent = (t->data.value.components.hi >> 20) & ((1 << 11) - 1);
    mant = (t->data.value.components.hi & ((1 << 20) - 1)) * POW2(32);
    mant += t->data.value.components.lo;
  } else {
    sign = t->data.value.components.lo >> 31;
    exponent = (t->data.value.components.lo >> 23) & ((1 << 8) - 1);
    mant = t->data.value.components.lo & ((1 << 23) - 1);
  }

  mant /= POW2(mantbits);
  if (exponent) mant += 1.0; /* restore leading 1 */
  else exponent = 1; /* subnormal */
  exponent -= bias;

  /* restore original value */
  while (exponent > 0) mant *= 2.0, exponent--;
  while (exponent < 0) mant /= 2.0, exponent++;
  t->data.value.f64 = mant * (sign ? -1 : 1);
}

static mpack_value_t pack_double(double v)
{
  return pack_ieee754(v, 52, 11);
}

static mpack_uint32_t pack_float(float v)
{
  return pack_ieee754(v, 23, 8).components.lo;
}

