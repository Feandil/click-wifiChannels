#ifndef ZUTIL_H
#define ZUTIL_H

#include <zlib.h>
#include <stdio.h>
#include <stdbool.h>

/** @file zutil.h Headers for a small simplified interface with zlib */

/**
 * Size of the buffer between zlib and the output file (for zutil_write).
 */
#define OUT_BUF_SIZE  1500

/**
 * Size of the buffer between zlib and the zutil client.
 */
#define IN_BUF_SIZE   4*4096

/**
 * Size of the buffer between the input file and zlib (for zutil_read).
 */
#define INC_BUF_SIZE  4096


/**
 * "Opaque" structure for writing operations.
 */
struct zutil_write {
  char out[OUT_BUF_SIZE]; //!< Buffer used between zlib and the output file.
  z_stream strm;          //!< Opaque structure from zlib.
  FILE *output;           //!< Output file.
};

/**
 * "Opaque" structure for reading operations
 */
struct zutil_read {
  char in[(2 * IN_BUF_SIZE) + 1]; /**< Buffer between zlib and the zutil client.
                                   * It is divided in two parts, the first IN_BUF_SIZE bytes
                                   * and the next IN_BUF_SIZE bytes (the 1 here is for adding a \0 on some cases)
                                   */
  char inc[INC_BUF_SIZE];         //!< Buffer between the input file and zlib.
  z_stream strm;                  //!< Opaque structure from zlib.
  char *start;                    //!< Pointer to the next byte to be read by the client.
  char *end;                      //!< Pointer to the last byte to be read by the client.
  bool swapped;                   /**< Indication to which part of the 'in' buffer is currently used.
                                   * true indicates that the second part contains the more recent zlib output
                                   * false indicates that the first part contains the more recent zlib output
                                   * (Except during the initialization or internal zutil_read_input calls)
                                   */
  FILE *input;                    //!< Input file.
};

/**
 * Open a new encoding stream.
 * @param buffer Zeroed memory pointer for the internal opaque structure.
 * @param out    Output file.
 * @param encode Level of the zlib encoding (1-9).
 * @return 0 in case of succes, -1 in case of error.
 */
int zinit_write(struct zutil_write* buffer, FILE *out, const int encode);

/**
 * Add data to a encoding stream (call exit(1) in case of failure).
 * @param in   Opaque structure describing the stream, need to be initialized by zinit_write.
 * @param data Pointer to the memory zone to be added to the stream.
 * @param len  Length of memory zone to be added to the stream.
 */
void zadd_data(struct zutil_write *in, const char *data, const size_t len);

/**
 * Close a opened encoding stream  (Close the file used for the output).
 * @param in Opaque structure describing the stream, need to be initialized by zinit_write and not closed.
 */
void zend_data(struct zutil_write *in);

/**
 * Open a new decoding stream.
 * @param buffer Zeroed memory pointer for the internal opaque structure.
 * @param in     Input file.
 * @return 0 in case of succes, -1 in case of error.
 */
int zinit_read(struct zutil_read* buffer, FILE *in);

/**
 * Read a new line (ended by '\n' or EOF) from the decoding stream
 * @param buffer The opaque structure describing the decoding stream, need to have been initialized by 'zinit_read'
 * @param len    Pointer to a ssize_t in which the length of the new line will be placed
 * @return A '\0' ended string which length is contain in len, NULL in case of failure (len will contain the error, -1 for EOF)
 */
char* zread_line(struct zutil_read *buffer, ssize_t *len);

/**
 * Close a decoding stream, close the corresponding file
 * @param buffer The opaque structure describing the decoding stream, need to have been initialized by 'zinit_read'
 */
void zread_end(struct zutil_read *buffer);

#endif /* ZUTIL_H */
