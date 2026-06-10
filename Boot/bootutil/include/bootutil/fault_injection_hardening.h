#ifndef H_FIH_H
#define H_FIH_H

typedef int fih_int;
typedef int fih_ret;

#define FIH_SUCCESS 0
#define FIH_FAILURE 1
#define FIH_NO_BOOTABLE_IMAGE 2

#define FIH_DECLARE(var, val) fih_ret var = (val)
#define FIH_SET(var, val) ((var) = (val))
#define FIH_EQ(x, y) ((x) == (y))
#define FIH_NOT_EQ(x, y) ((x) != (y))
#define FIH_RET(x) return (x)

#define FIH_CALL(f, ret, ...) \
    do { \
        (ret) = f(__VA_ARGS__); \
    } while (0)

static inline fih_ret fih_ret_encode_zero_equality(int x)
{
    return (x == 0) ? FIH_SUCCESS : FIH_FAILURE;
}

#endif /* H_FIH_H */
