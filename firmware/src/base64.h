#include <stddef.h>

/* URL-safe base64 encode */
int base64_encode(char *output, size_t output_size, unsigned char *input, size_t length);
/* URL-safe base64 decode */
int base64_decode(unsigned char *output, size_t output_size, char *input, size_t length);