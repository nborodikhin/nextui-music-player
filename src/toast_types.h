#ifndef __TOAST_TYPES_H__
#define __TOAST_TYPES_H__

#include <stdint.h>

typedef uint32_t ToastToken;
// Zero is never a token assigned to a toast.
#define TOAST_TOKEN_NONE ((ToastToken)0)

// Default lifetime for toasts.
#define TOAST_DURATION 3000
// For manually managed toasts that must not expire automatically.
#define TOAST_DURATION_FOREVER UINT32_MAX

#endif
