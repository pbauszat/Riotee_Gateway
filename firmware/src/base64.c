#include <stdio.h>
#include <string.h>

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int base64_encode(char *output, size_t output_size, unsigned char *input, size_t length) {
  int i, j;
  int padding = 0;

  size_t encoded_length = (length + 2) / 3 * 4;  // Calculate the encoded length

  if (output_size < encoded_length + 1)  // Check if output buffer is large enough
    return -1;

  for (i = 0, j = 0; i < length; i += 3) {
    unsigned char a = input[i];
    unsigned char b = (i + 1 < length) ? input[i + 1] : 0;
    unsigned char c = (i + 2 < length) ? input[i + 2] : 0;

    output[j++] = base64_table[a >> 2];
    output[j++] = base64_table[((a & 0x03) << 4) | (b >> 4)];
    output[j++] = base64_table[((b & 0x0F) << 2) | (c >> 6)];
    output[j++] = base64_table[c & 0x3F];
  }

  // Add padding characters if necessary
  if (length % 3 == 1) {
    output[j - 2] = '=';
    output[j - 1] = '=';
    padding = 2;
  } else if (length % 3 == 2) {
    output[j - 1] = '=';
    padding = 1;
  }

  output[j] = '\0';  // Null-terminate the string

  return j;
}

int base64_decode(unsigned char *output, size_t output_size, char *input, size_t length) {
  int i, j;
  int padding = 0;

  size_t decoded_length = (length * 3) / 4;  // Calculate the maximum decoded length

  if (input[length - 1] == '=') {
    padding++;
    if (input[length - 2] == '=')
      padding++;
  }

  if (output_size < (decoded_length - padding))
    return -1;

  for (i = 0, j = 0; i < length; i += 4) {
    unsigned char a = strchr(base64_table, input[i]) - base64_table;
    unsigned char b = strchr(base64_table, input[i + 1]) - base64_table;
    unsigned char c = strchr(base64_table, input[i + 2]) - base64_table;
    unsigned char d = strchr(base64_table, input[i + 3]) - base64_table;

    output[j++] = (a << 2) | (b >> 4);
    if (input[i + 2] != '=')
      output[j++] = (b << 4) | (c >> 2);
    if (input[i + 3] != '=')
      output[j++] = (c << 6) | d;
  }

  return j;
}
