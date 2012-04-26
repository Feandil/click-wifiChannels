#ifndef BASICONOFF_H
#define BASICONOFF_H

#include "module.h"
#include <getopt.h>
#include <map>
#include <iostream>
#include <fstream>

class ParamBasicOnOff : public ParamModule {

  private:

    /* static stuff */
    static const struct option long_options[];
    static const char * const needfiles;

    /* Variables */
    uint64_t success_total;
    uint64_t error_total;

    std::map<uint32_t, uint64_t> success_length;
    std::map<uint32_t, uint64_t> error_length;

    std::map<uint32_t, uint32_t> success_length_final;
    std::map<uint32_t, uint32_t> error_length_final;

    bool current_state;
    uint64_t length;

    /* Output */
    const char *error_filename, *free_filename;

    /* Error String */
    static const char * const knotset;

    /* functions */
    static void calculate_values(const uint32_t, const std::map<uint32_t, uint64_t>&, const uint64_t, std::map<uint32_t, uint32_t>&);
    static void printBinaryToFile(const std::map<uint32_t, uint32_t>&, const char*);
    static void printHumanToStream(const uint32_t, const std::map<uint32_t, uint32_t>&, std::ostream&);

  public:

    /* Initialize the module */
    int init(const int, char **, const bool, const char**);
    int init(const char * const, const char * const);

    /* Clean the module */
    void clean();

    /* Add input char */
    int addChar(const bool);

    /* This function must not be used at the same time as addChar. Two consecutive addChars calls must not be called with the same bool */
    int addChars(const bool, uint32_t len);

    /* Is-there a 2nd round ? (prepare the module to the potential 2nd round */
    bool nextRound();

    /* Get direct source data */
    const std::map<uint32_t, uint64_t>* getRawErrorFreeBurstLengthCDF(void) { return &success_length; }
    const uint64_t getRawErrorFreeBurstNumber(void) { return success_total; }
    const std::map<uint32_t, uint64_t>* getRawErrorBurstLengthCDF(void) { return &error_length; }
    const uint64_t getRawErrorBurstNumber(void) { return error_total; }
    
    /* Finalise the data */
    void finalize(const uint32_t);

    /* Get direct output data */
    const std::map<uint32_t, uint32_t>* getErrorFreeBurstLengthCDF(void) { return &success_length_final; }
    const std::map<uint32_t, uint32_t>* getErrorBurstLengthCDF(void) { return &error_length_final; }

    /* Output */
    void printBinary();
    void printHuman(const uint32_t);

    /* name */
    static const char* name() { return "basiconoff"; }
};

#endif
