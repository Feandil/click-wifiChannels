#ifndef BASICONOFF_H
#define BASICONOFF_H

#include "module.h"
#include <getopt.h>
#include <map>
#include <iostream>
#include <fstream>

/**
 * Extract a Basic On-Off representation.
 * Calculate the distribution length of 0's or 1's bursts
 * Return the Cumulative Distribution Functions for those two distributions
 */
class ParamBasicOnOff : public ParamModule {

  private:
    /**
     * Long options used by getopt_long.
     */
    static const struct option long_options[];
    /**
     * Error message: This module need more files
     */
    static const char * const needfiles;

    /* Variables */
    //! Total number of success bursts
    uint64_t success_total;
    //! Total number of error bursts
    uint64_t error_total;

    //! Collection of (length,number of error-free bursts of that lenght)
    std::map<uint32_t, uint64_t> success_length;
    //! Collection of (length,number of error bursts of that lenght)
    std::map<uint32_t, uint64_t> error_length;

    //! Collection of (length, Cumulated probability, in regard of rand_max, of an error-free bursts of that lenght)
    std::map<uint32_t, uint32_t> success_length_final;
    //! Collection of (length, Cumulated probability, in regard of rand_max, of an error bursts of that lenght)
    std::map<uint32_t, uint32_t> error_length_final;

    //! State of the last packet (success/error)
    bool current_state;
    //! Duration of the last state (number of consecutive packets in that state)
    uint32_t length;

    /* Output */
    //! Ouput file for the error bursts distribution
    const char *error_filename;
   //! Ouput file for the error-free bursts distribution
    const char *free_filename;

    /* Helper functions */
    /**
     * Extract '(*)_length_final' from '\1_length'.
     * @param max_rand CLICK_RAND_MAX used by click
     * @param source '\1_length'
     * @param nb Number of elements
     * @param dest '(*)_length_final'
    */
    static void calculate_values(const uint32_t max_rand, const std::map<uint32_t, uint64_t>& source, const uint64_t nb, std::map<uint32_t, uint32_t>& dest);

    /**
     * Print a distribution to a file, in 'binary' format
     * @param distribution Distribution to be printed
     * @param filename Name of the file in which we will print the output
     */
    static void printBinaryToFile(const std::map<uint32_t, uint32_t>& distribution, const char* filename);
    /**
     * Print a distribution to a file, in human-readable format
     * @param max_rand CLICK_RAND_MAX used by click
     * @param distribution Distribution to be printed
     * @param destination Destination in which we will print the output
     */
    static void printHumanToStream(const uint32_t max_rand, const std::map<uint32_t, uint32_t>& distribution, std::ostream& destination);

  public:
    /* Methodes of ParamModule */
    int init(const int, char **, const bool, const char**);
    void clean();
    int addChar(const bool);
    bool nextRound();
    void finalize(const uint32_t);
    void printBinary();
    void printHuman(const uint32_t);

    /**
     * Special module-dependant initialization
     * @param filename_error Name of the file used for printing the error-bursts length distribution
     * @param filename_free  Name of the file used for printing the error-free-bursts length distribution
     * @return Ok: 0, anything else in case of error (error code)
     */
    int init(const char * const filename_error, const char * const filename_free);

    /**
     * Register directly bursts and not symbol by symbol.
     * \warning
     *  - This function doesn't any of the internal states used by addChar, thus theses two functions must not be used at the same time.
     *  - Two consecutive addChars calls must be called with different 'in' values.
     * @param in True if the burst of packet was received, False if it wasn't
     * @param len Length of the burst
     * @return Ok: 0, anything else in case of error (error code)
     */
    int addChars(const bool in, uint32_t len);

    /* Get direct source data */
    //! Get the raw 'success_length'
    const std::map<uint32_t, uint64_t>* getRawErrorFreeBurstLengthCDF(void) { return &success_length; }
    //! Get the total number of error-free bursts
    uint64_t getRawErrorFreeBurstNumber(void) { return success_total; }
    //! Get the raw 'error_length'
    const std::map<uint32_t, uint64_t>* getRawErrorBurstLengthCDF(void) { return &error_length; }
    //! Get the total number of error bursts
    uint64_t getRawErrorBurstNumber(void) { return error_total; }

    /* Get direct output data */
    //! Get the raw 'success_length_final'
    const std::map<uint32_t, uint32_t>* getErrorFreeBurstLengthCDF(void) { return &success_length_final; }
    //! Get the raw 'error_length_final'
    const std::map<uint32_t, uint32_t>* getErrorBurstLengthCDF(void) { return &error_length_final; }

    //! Name of this module
    static const char* name() { return "basiconoff"; }
};

#endif
